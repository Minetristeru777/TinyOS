#!/usr/bin/env lua54
-- Build a MINIFS1, CFS1, or CFS2 disk image from a directory of files.

local function usage()
    io.stderr:write("Usage: ./lua54 import_disk.lua --folder exported-files [--file disk.bin] [--format minifs1|cfs1|cfs2] [--force]\n")
    io.stderr:write("Existing images are backed up to FILE.bak before --force replaces them.\n")
    io.stderr:write("The image is rebuilt from the files in the folder; subfolders are ignored.\n")
end

local function fail(message)
    io.stderr:write("import_disk: " .. message .. "\n")
    os.exit(1)
end

local function shell_quote(value)
    return "'" .. value:gsub("'", "'\\''") .. "'"
end

local input_dir, image_path, requested_format, force
local file_option_seen = false
image_path = "disk.bin"
local i = 1
while i <= #arg do
    local option = arg[i]
    if option == "--help" or option == "-h" then
        usage()
        os.exit(0)
    elseif option == "--force" then
        force = true
    elseif option == "--folder" or option == "--file" or option == "--format" then
        i = i + 1
        if not arg[i] or arg[i]:sub(1, 2) == "--" then fail(option .. " requires a value") end
        if option == "--folder" then
            if input_dir then fail("--folder was specified more than once") end
            input_dir = arg[i]
        elseif option == "--file" then
            if file_option_seen then fail("--file was specified more than once") end
            image_path = arg[i]
            file_option_seen = true
        else
            if requested_format then fail("--format was specified more than once") end
            requested_format = arg[i]:lower()
        end
    else
        fail("unknown option: " .. option)
    end
    i = i + 1
end
if not input_dir then usage(); fail("--folder is required") end
if requested_format and requested_format ~= "minifs1" and requested_format ~= "cfs1" and requested_format ~= "cfs2" then
    fail("format must be minifs1, cfs1, or cfs2")
end

local function read_u32(data, offset)
    local a, b, c, d = data:byte(offset, offset + 3)
    if not d then return nil end
    return a + b * 256 + c * 65536 + d * 16777216
end

local function u16le(value)
    return string.char(value % 256, math.floor(value / 256) % 256)
end

local function u32le(value)
    return string.char(value % 256, math.floor(value / 256) % 256,
                       math.floor(value / 65536) % 256, math.floor(value / 16777216) % 256)
end

local function exists(path)
    local file = io.open(path, "rb")
    if not file then return false end
    file:close()
    return true
end

local function detect_format(path)
    local file = io.open(path, "rb")
    if not file then return nil end
    local header = file:read(12) or ""
    file:close()
    if header:sub(1, 7) == "MINIFS1" and read_u32(header, 9) == 1 then return "minifs1" end
    if header:sub(1, 4) == "CFS1" and header:byte(5) == 1 then return "cfs1" end
    if header:sub(1, 4) == "CFS2" and header:byte(5) == 2 then return "cfs2" end
    return nil
end

local format = requested_format or detect_format(image_path) or "cfs2"

local function list_files(folder)
    local test_ok = os.execute("test -d " .. shell_quote(folder))
    if not test_ok then fail("not a directory: " .. folder) end
    local command = "find " .. shell_quote(folder) .. " -mindepth 1 -maxdepth 1 -type f -print0"
    local pipe, pipe_error = io.popen(command, "r")
    if not pipe then fail("cannot list " .. folder .. ": " .. tostring(pipe_error)) end
    local output = pipe:read("*a") or ""
    local close_ok, _, code = pipe:close()
    if not close_ok then fail("failed to list " .. folder .. " (exit " .. tostring(code) .. ")") end

    local files = {}
    for path in output:gmatch("([^\0]+)\0") do
        local name = path:match("([^/]*)$")
        if not name or name == "" or name == "." or name == ".." then
            fail("invalid filename in input folder")
        end
        for j = 1, #name do
            local byte = name:byte(j)
            if byte < 32 or byte == 127 or name:sub(j, j) == "\\" then
                fail("unsupported character in filename: " .. name)
            end
        end
        local file, open_error = io.open(path, "rb")
        if not file then fail("cannot read " .. path .. ": " .. tostring(open_error)) end
        local contents = file:read("*a")
        file:close()
        files[#files + 1] = {name = name, contents = contents}
    end
    table.sort(files, function(a, b) return a.name < b.name end)
    return files
end

local files = list_files(input_dir)

local function check_names_and_count(max_count, max_name_length, uppercase)
    if #files > max_count then fail(string.format("%s supports at most %d files", format, max_count)) end
    local seen = {}
    for _, file in ipairs(files) do
        if uppercase then file.name = file.name:upper() end
        if #file.name > max_name_length then
            fail(string.format("filename too long for %s: %s (max %d bytes)", format, file.name, max_name_length))
        end
        if file.name:find("/", 1, true) or file.name == "" or file.name == "." or file.name == ".." then
            fail("unsafe filename: " .. file.name)
        end
        if seen[file.name] then fail("duplicate filename after format normalization: " .. file.name) end
        seen[file.name] = true
    end
end

local function make_minifs1()
    check_names_and_count(16, 31, false)
    local entries = {}
    for _, file in ipairs(files) do
        if #file.contents > 192 then fail("MINIFS1 file exceeds 192 bytes: " .. file.name) end
        entries[#entries + 1] = file.name .. string.rep("\0", 32 - #file.name)
            .. u32le(2) .. u32le(#file.contents)
            .. file.contents .. string.rep("\0", 192 - #file.contents)
    end
    while #entries < 16 do entries[#entries + 1] = string.rep("\0", 232) end
    local image = "MINIFS1\0" .. u32le(1) .. table.concat(entries)
    return image .. string.rep("\0", 4096 - #image)
end

local function make_cfs1()
    check_names_and_count(15, 23, false)
    local entries = {}
    for _, file in ipairs(files) do
        if #file.contents > 228 then fail("CFS1 file exceeds 228 bytes: " .. file.name) end
        entries[#entries + 1] = file.name .. string.rep("\0", 24 - #file.name)
            .. string.char(2) .. u16le(#file.contents) .. "\0"
            .. file.contents .. string.rep("\0", 228 - #file.contents)
    end
    while #entries < 15 do entries[#entries + 1] = string.rep("\0", 256) end
    local image = "CFS1" .. string.char(1) .. string.rep("\0", 11) .. table.concat(entries)
    return image .. string.rep("\0", 4096 - #image)
end

local function make_cfs2()
    check_names_and_count(16, 19, true)
    local block_size, block_count = 512, 126
    local fat = {}
    local data_blocks = {}
    for block = 0, block_count - 1 do
        fat[block + 1] = string.char(0xff)
        data_blocks[block + 1] = string.rep("\0", block_size)
    end

    local directory, next_block = {}, 0
    for _, file in ipairs(files) do
        for j = 1, #file.name do
            local byte = file.name:byte(j)
            if not ((byte >= 65 and byte <= 90) or (byte >= 48 and byte <= 57) or byte == 46 or byte == 45) then
                fail("CFS2 names allow only A-Z, 0-9, dot, and dash: " .. file.name)
            end
        end
        local size = #file.contents
        local needed = math.floor((size + block_size - 1) / block_size)
        if next_block + needed > block_count then fail("CFS2 image has only 63 KiB for file data") end
        local first_block = needed == 0 and 65535 or next_block
        directory[#directory + 1] = file.name .. string.rep("\0", 20 - #file.name)
            .. u32le(size) .. u16le(first_block) .. string.char(1, 0) .. string.rep("\0", 4)
        for offset = 0, needed - 1 do
            local block = next_block + offset
            local start = offset * block_size + 1
            local contents = file.contents:sub(start, start + block_size - 1)
            data_blocks[block + 1] = contents .. string.rep("\0", block_size - #contents)
            fat[block + 1] = string.char(offset + 1 < needed and (block + 1) or 0xfe)
        end
        next_block = next_block + needed
    end
    while #directory < 16 do directory[#directory + 1] = string.rep("\0", 32) end

    local header = "CFS2" .. string.char(2, 2, block_count, 2) .. string.rep("\0", 24)
    local metadata = header .. table.concat(directory) .. table.concat(fat)
    metadata = metadata .. string.rep("\0", 1024 - #metadata)
    return metadata .. table.concat(data_blocks)
end

local image
if format == "minifs1" then
    image = make_minifs1()
elseif format == "cfs1" then
    image = make_cfs1()
else
    image = make_cfs2()
end

if exists(image_path) and not force then
    fail(image_path .. " already exists; use --force to replace it (a .bak copy will be made)")
end

local temporary = image_path .. ".tmp." .. tostring(os.time())
local temp_file, temp_error = io.open(temporary, "wb")
if not temp_file then fail("cannot create temporary image " .. temporary .. ": " .. tostring(temp_error)) end
local write_ok, write_error = temp_file:write(image)
local close_ok, close_error = temp_file:close()
if not write_ok or not close_ok then
    os.remove(temporary)
    fail("could not write complete image: " .. tostring(write_error or close_error))
end

local backup_path
if exists(image_path) then
    backup_path = image_path .. ".bak"
    local suffix = 1
    while exists(backup_path) do
        backup_path = image_path .. ".bak." .. suffix
        suffix = suffix + 1
    end
    local backup_ok, backup_error = os.rename(image_path, backup_path)
    if not backup_ok then os.remove(temporary); fail("cannot back up image: " .. tostring(backup_error)) end
end

local rename_ok, rename_error = os.rename(temporary, image_path)
if not rename_ok then
    if backup_path then os.rename(backup_path, image_path) end
    os.remove(temporary)
    fail("cannot install new image: " .. tostring(rename_error))
end

io.write(string.format("Wrote %s image (%d files, %d bytes) to %s\n", format, #files, #image, image_path))
if backup_path then io.write("Previous image saved as " .. backup_path .. "\n") end
