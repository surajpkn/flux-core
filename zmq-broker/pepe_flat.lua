local hostlist = require ("hostlist")
local tree = require ("tree")

if pepe.rank == 0 then
    local env = pepe:getenv()
    local v,err = pepe:getenv("ENV")
    if not v then
	print ("getenv(ENV): " .. err .. "\n")
    end
    pepe:unsetenv ("ENV")
    pepe:setenv ("HAVE_PEPE", 1)
    pepe:setenv ("PS1", "${SLURM_JOB_NODELIST} \\\u@\\\h \\\w$ ")

end

local h = hostlist.new (pepe.nodelist)
local eventuri = "epgm://eth0;239.192.1.1:5555"
local treeinuri = "tcp://*:5556"
local treeouturi = "tcp://" ..  h[1] .. ":5556"
local child_opt = tree.k_ary_children (pepe.rank, #h - 1, #h)
if string.len (child_opt) > 0 then
    child_opt = " --children=" .. child_opt
end

if pepe.rank == 0 then
    pepe.run ("echo bind 127.0.0.1 | /usr/sbin/redis-server -")
    pepe.run ("./cmbd --event-uri='" .. eventuri .. "'"
		.. " --tree-in-uri='" .. treeinuri .. "'"
		.. " --redis-server=localhost"
		.. " --rank=" .. pepe.rank
		.. " --size=" .. #h
		.. " --plugins=api,barrier,live,log,kvs,sync"
		.. child_opt)
else
    pepe.run ("./cmbd --event-uri='" .. eventuri .. "'"
		.. " --parent='0," .. treeouturi .. "'"
		.. " --rank=" .. pepe.rank
		.. " --plugins=api,barrier,live,log"
		.. " --size=" .. #h)
end
