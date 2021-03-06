from _kvs import ffi, lib
import flux
from flux.wrapper import Wrapper, WrapperPimpl, WrapperBase
import flux.json_c as json_c
import json
import collections
import contextlib
import errno
import os
import sys

class KVSWrapper(Wrapper):
  # This empty class accepts new methods, preventing accidental overloading
  # across wrappers
  pass

_raw = KVSWrapper(ffi, lib, prefixes=['kvs', 'kvs_'])
# override error check behavior for kvsitr_next
_raw.kvsitr_next.set_error_check(lambda x: False)


def get_key_direct(flux_handle, key):
    j = json_c.Jobj()
    _raw.get_obj(flux_handle, key, j.get_as_dptr())
    return json.loads(j.as_str())


def exists(flux_handle, key):
    try:
        get_key_direct(flux_handle, key)
        return True
    except EnvironmentError as err:
        if err.errno == errno.ENOENT:
            return False
        if err.errno == errno.EISDIR:
            return True
        raise err


def isdir(flux_handle, key):
    try:
        get_key_direct(flux_handle, key)
    except EnvironmentError as err:
        if err.errno == errno.EISDIR:
            return True
        raise err
    return False


def get_dir(flux_handle, key=''):
    return KVSDir(path=key, flux_handle=flux_handle)


def get(flux_handle, key):
    try:
        return get_key_direct(flux_handle, key)
    except EnvironmentError as err:
        if err.errno == errno.EISDIR:
            pass
        else:
            raise err
    return get_dir(flux_handle, key)

def put(flux_handle, key, value):
    j = json_c.Jobj(json.dumps(value))
    _raw.put_obj(flux_handle, key, j.get())


def commit(flux_handle):
    return _raw.kvs_commit(flux_handle)


def watch_once(flux_handle, key):
    """ Watches the selected key until the next change, then returns the updated value of the key """
    if isdir(flux_handle, key):
        d = get_dir(flux_handle)
        # The wrapper automatically unpacks d's handle
        _raw.watch_once_dir(flux_handle, d)
        return d
    else:
        j = json_c.Jobj()
        _raw.watch_once(flux_handle, key, j.get_as_dptr())
        return j.as_str()


class KVSDir(WrapperPimpl, collections.MutableMapping):
    class InnerWrapper(Wrapper):
        def __init__(self, flux_handle=None, path='.', handle=None):
            self.handle = None
            if flux_handle is None and handle is None:  # pragma: no cover
                raise ValueError(
                    "flux_handle must be a valid Flux object or handle must be a valid kvsdir cdata pointer")
            if handle is None:
                d = ffi.new("kvsdir_t *[1]")
                _raw.kvs_get_dir(flux_handle, d, path)
                handle = d[0]

            super(self.__class__, self).__init__(ffi, lib,
                                                 handle=handle,
                                                 match=ffi.typeof('kvsdir_t *'),
                                                 prefixes=[
                                                     'kvsdir_',
                                                 ], )

        def __del__(self):
            if self.handle is not None:
                _raw.kvsdir_destroy(self.handle)

    def __init__(self, flux_handle=None, path='.', handle=None):
        self.fh = flux_handle
        self.path = path
        if flux_handle is None and handle is None:
            raise ValueError(
                "flux_handle must be a valid Flux object or handle must be a valid kvsdir cdata pointer")
        self.pimpl = self.InnerWrapper(flux_handle, path, handle)

    def commit(self):
        commit(self.fh.handle)

    def key_at(self, key):
        c_str = self.pimpl.key_at(key)
        p_str = ffi.string(c_str)
        lib.free(c_str)
        return p_str

    def __getitem__(self, key):
        try:
            return get(self.fh, self.key_at(key))
        except EnvironmentError:
            raise KeyError(
                '{} not found under directory {}'.format(key, self.key_at('')))

    def __setitem__(self, key, value):
        # Turn it into json
        j = json_c.Jobj(json.dumps(value))
        self.pimpl.put_obj(key, j.get())

    def __delitem__(self, key):
        self.pimpl.unlink(key)

    class KVSDirIterator(collections.Iterator):
        def __init__(self, kd):
            self.kd = kd
            self.itr = None
            self.itr = _raw.kvsitr_create(kd.handle)

        def __del__(self):
            _raw.kvsitr_destroy(self.itr)

        def __iter__(self):
            return self

        def __next__(self):
            ret = _raw.kvsitr_next(self.itr)
            if ret is None or ret == ffi.NULL:
                raise StopIteration()
            return ffi.string(ret)

        def next(self):
            return self.__next__()

    def __iter__(self):
        return self.KVSDirIterator(self)

    def __len__(self):
        return self.pimpl.get_size()

    def fill(self, contents):
        """ Populate this directory with keys specified by contents, which must
    conform to the Mapping interface

    :param contents: A dict of keys and values to be created in the directory
      or None, sub-directories can be created by using `dir.file` syntax,
      sub-dicts will be stored as json values in a single key """

        if contents is None:
            raise ValueError("contents must be non-None")

        with self as kd:
            for k, v in contents.items():
                self[k] = v

    def mkdir(self, key, contents=None):
        """ Create a new sub-directory, optionally pre-populated with the contents
    of `files` as would be done with `fill(contents)`

    :param key: Key of the directory to be created
    :param contents: A dict of keys and values to be created in the directory
      or None, sub-directories can be created by using `dir.file` syntax,
      sub-dicts will be stored as json values in a single key """

        self.pimpl.mkdir(key)
        # TODO : find a way to aggregate past mkdir commands
        self.commit()
        new_kvsdir = KVSDir(self.fh, key)
        if contents is not None:
            new_kvsdir.fill(contents)

    def files(self):
        for k in self:
            if not self.pimpl.isdir(k):
                yield k

    def directories(self):
        for k in self:
            if self.pimpl.isdir(k):
                yield k

    def list_all(self, topdown=False):
        files = []
        dirs = []
        for k in self:
            if self.pimpl.isdir(k):
                dirs.append(k)
            else:
                files.append(k)
        return (files, dirs)

    def __enter__(self):
        """Allow this to be used as a context manager"""
        return self

    def __exit__(self, type_arg, value, tb):
        """ When used as a context manager, the KVSDir commits itself on exit """
        self.commit()
        return False

    def watch_once(self, flux_handle, key):
        """ Watches the selected key until the next change, then returns the updated value of the key """
        j = json_c.Jobj()
        self.pimpl.kvs_watch_once_dir(self.fh, self.handle, j.get_as_dptr())
        return j.as_str()


def join(*args):
    return ".".join([a for a in args if len(a) > 0])


def inner_walk(kd, curr_dir, topdown=False):
    if topdown:
        yield (curr_dir, kd.directories(), kd.files())

    for d in kd.directories():
        path = join(curr_dir, d)
        for x in inner_walk(get_dir(kd.fh, kd.key_at(d)), path, topdown):
            yield x

    if not topdown:
        yield (curr_dir, kd.directories(), kd.files())


def walk(directory, topdown=False, flux_handle=None):
    """ Walk a directory in the style of os.walk() """
    if not isinstance(directory, KVSDir):
        if flux_handle is None:
            raise ValueError(
                "If directory is a key, flux_handle must be specified")
        directory = KVSDir(flux_handle, directory)
    return inner_walk(directory, '', topdown)

@ffi.callback('kvs_set_obj_f')
def KVSWatchWrapper(key, value, arg, errnum):
    j = Jobj(handle=value)
    (cb, real_arg) = ffi.from_handle(arg)
    return cb(key, json.loads(j.as_str()), real_arg, errnum)


kvswatches = {}


def watch(flux_handle, key, fun, arg):
    warg = (fun, arg)
    kvswatches[key] = warg
    return _raw.watch(flux_handle, key, KVSWatchWrapper, ffi.new_handle(warg))


def unwatch(flux_handle, key):
    kvswatches.pop(key, None)
    return _raw.unwatch(flux_handle, key)
