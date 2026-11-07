#!/usr/bin/env lua54
-- Export files from the sandbox MINIFS1 disk or the OS CFS1/CFS2 disk.

local function usage()
    io.stderr:write("Usage: ./lua54 export_disk.lua --file disk.bin [--output folder] [--force]\n")
    io.stderr:write("Defaults: --file disk.bin --output disk-export\n")
end

local function fail(message)
    io.stderr:write("export_disk: " .. message .. "\n")
    os.exit(1)
end

local disk_path, output_dir, force
local positional, i = {}, 1
while i <= #arg do
    local value = arg[i]
    if value == "--help" or value == "-h" then
        usage()
        os.exit(0)
    elseif value == "--force" then
        force = true
    elseif value == "--file" or value == "--output" or value == "--out" then
        local option = value
        i = i + 1
        if not arg[i] or arg[i]:sub(1, 2) == "--" then
            fail(option .. " requires a value")
        end
        if option == "--file" then
            if disk_path then fail("--file was specified more than once") end
            disk_path = arg[i]
        else
            if output_dir then fail("--output was specified more than once") end
            output_dir = arg[i]
        end
    else
        positional[#positional + 1] = value
    end
    i = i + 1
end
if #positional > 2 then
    usage()
    os.exit(2)
end
if positional[1] then
    if disk_path then fail("disk image specified both positionally and with --file") end
    disk_path = positional[1]
end
if positional[2] then
    if output_dir then fail("output folder specified more than once") end
    output_dir = positional[2]
end
disk_path = disk_path or "disk.bin"
output_dir = output_dir or "disk-export"

local function read_u16(data, offset)
    local a, b = data:byte(offset, offset + 1)
    if not b then return nil end
    return a + b * 256
end

local function read_u32(data, offset)
    local a, b, c, d = data:byte(offset, offset + 3)
    if not d then return nil end
    return a + b * 256 + c * 65536 + d * 16777216
end

local function read_c_string(data, max_length)
    local stop = data:find("\0", 1, true)
    if stop then
        return data:sub(1, math.min(stop - 1, max_length))
    end
    return data:sub(1, max_length)
end

local function valid_filename(name)
    if name == "" or name == "." or name == ".." then return false end
    for i = 1, #name do
        local byte = name:byte(i)
        if byte < 32 or byte == 127 or name:sub(i, i) == "/" or name:sub(i, i) == "\\" then
            return false
        end
    end
    return true
end

local function add_file(files, name, contents)
    if not valid_filename(name) then
        fail("unsafe or empty filename in disk image")
    end
    if files[name] then
        fail("duplicate filename in disk image: " .. name)
    end
    files[name] = contents
end

local function parse_minifs1(data)
    if #data < 12 then fail("MINIFS1 image is shorter than its header") end
    local version = read_u32(data, 9)
    if version ~= 1 then fail("unsupported MINIFS1 version: " .. tostring(version)) end

    local files, directories = {}, 0
    local entry_size, entry_count, entry_base = 232, 16, 13
    if #data < entry_base - 1 + entry_size * entry_count then
        fail("MINIFS1 image is too short for its directory")
    end
    for index = 0, entry_count - 1 do
        local start = entry_base + index * entry_size
        local entry = data:sub(start, start + entry_size - 1)
        local name = read_c_string(entry:sub(1, 32), 32)
        if name ~= "" then
            local kind = read_u32(entry, 33)
            local size = read_u32(entry, 37)
            if kind == 1 then
                directories = directories + 1
            elseif kind == 2 then
                if not size or size > 192 then
                    fail("invalid file size for " .. name)
                end
                add_file(files, name, entry:sub(41, 40 + size))
            else
                fail("unknown MINIFS1 entry type for " .. name)
            end
        end
    end
    return files, directories, "MINIFS1"
end

local function parse_cfs1(data)
    if #data < 4096 then fail("CFS1 image is shorter than 4 KiB") end
    if data:byte(5) ~= 1 then fail("unsupported CFS1 version") end

    local files = {}
    local entry_size, entry_count, entry_base = 256, 15, 17
    for index = 0, entry_count - 1 do
        local start = entry_base + index * entry_size
        local entry = data:sub(start, start + entry_size - 1)
        local name = read_c_string(entry:sub(1, 24), 24)
        if name ~= "" and entry:byte(25) == 2 then
            local size = read_u16(entry, 26)
            if not size or size > 228 then fail("invalid CFS1 file size for " .. name) end
            add_file(files, name, entry:sub(29, 28 + size))
        end
    end
    return files, 0, "CFS1"
end

local function parse_cfs2(data)
    local disk_size = 65536
    local metadata_size, block_size = 1024, 512
    local entry_base, entry_size, entry_count = 32, 32, 16
    local fat_offset, block_count = entry_base + entry_size * entry_count, 126
    if #data < disk_size then fail("CFS2 image is shorter than 64 KiB") end
    if data:byte(5) ~= 2 then fail("unsupported CFS2 version") end

    local files, owners = {}, {}
    for index = 0, entry_count - 1 do
        local start = entry_base + index * entry_size + 1
        local entry = data:sub(start, start + entry_size - 1)
        local flags = entry:byte(27)
        if flags and (flags & 1) ~= 0 then
            local name = read_c_string(entry:sub(1, 20), 20)
            local size = read_u32(entry, 21)
            local first_block = read_u16(entry, 25)
            if not size or size > (disk_size - metadata_size) then
                fail("invalid CFS2 file size for " .. name)
            end
            if size == 0 then
                if first_block ~= 65535 then fail("invalid empty-file block for " .. name) end
                add_file(files, name, "")
            else
                local blocks_needed = math.floor((size + block_size - 1) / block_size)
                local block, remaining, chunks = first_block, size, {}
                local visited = {}
                for _ = 1, blocks_needed do
                    if block >= block_count or visited[block] then
                        fail("invalid or cyclic CFS2 block chain for " .. name)
                    end
                    if owners[block] then
                        fail("CFS2 block is shared by " .. owners[block] .. " and " .. name)
                    end
                    visited[block], owners[block] = true, name
                    local amount = math.min(remaining, block_size)
                    local data_start = metadata_size + block * block_size + 1
                    chunks[#chunks + 1] = data:sub(data_start, data_start + amount - 1)
                    remaining = remaining - amount

                    local next_block = data:byte(fat_offset + block + 1)
                    if #chunks == blocks_needed then
                        if next_block ~= 0xfe then fail("CFS2 chain for " .. name .. " has no end marker") end
                    else
                        if next_block == 0xff or next_block == 0xfe or next_block >= block_count then
                            fail("CFS2 chain for " .. name .. " ends early")
                        end
                        block = next_block
                    end
                end
                add_file(files, name, table.concat(chunks))
            end
        end
    end
    return files, 0, "CFS2"
end

local disk_file, open_error = io.open(disk_path, "rb")
if not disk_file then fail("cannot open " .. disk_path .. ": " .. tostring(open_error)) end
local disk = disk_file:read("*a")
disk_file:close()

local files, directory_count, format
if disk:sub(1, 7) == "MINIFS1" then
    files, directory_count, format = parse_minifs1(disk)
elseif disk:sub(1, 4) == "CFS2" then
    files, directory_count, format = parse_cfs2(disk)
elseif disk:sub(1, 4) == "CFS1" then
    files, directory_count, format = parse_cfs1(disk)
else
    fail("unknown filesystem magic in " .. disk_path)
end

local function shell_quote(value)
    return "'" .. value:gsub("'", "'\\''") .. "'"
end
local mkdir_ok, mkdir_type, mkdir_code = os.execute("mkdir -p -- " .. shell_quote(output_dir))
if not mkdir_ok then
    fail("cannot create output directory " .. output_dir .. " (" .. tostring(mkdir_type) .. ", " .. tostring(mkdir_code) .. ")")
end

local written, skipped = 0, 0
for name, contents in pairs(files) do
    local path = output_dir .. (output_dir:sub(-1) == "/" and "" or "/") .. name
    local existing = io.open(path, "rb")
    if existing then
        existing:close()
        if not force then
            io.stderr:write("skip existing: " .. path .. " (use --force to replace)\n")
            skipped = skipped + 1
        else
            existing = assert(io.open(path, "wb"))
            assert(existing:write(contents))
            assert(existing:close())
            written = written + 1
        end
    else
        local out, write_error = io.open(path, "wb")
        if not out then fail("cannot write " .. path .. ": " .. tostring(write_error)) end
        local ok, err = out:write(contents)
        local close_ok, close_err = out:close()
        if not ok or not close_ok then fail("write failed for " .. path .. ": " .. tostring(err or close_err)) end
        written = written + 1
    end
end

io.write(string.format("%s: exported %d file(s) to %s", format, written, output_dir))
if directory_count > 0 then io.write(string.format("; skipped %d director%s", directory_count, directory_count == 1 and "y" or "ies")) end
if skipped > 0 then io.write(string.format("; skipped %d existing file(s)", skipped)) end
io.write("\n")
