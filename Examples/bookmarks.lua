-- Script function and purpose: a sofi mode written in Lua. Lists bookmarks read
-- from a plain-text file, opens the selected one with xdg-open, hides one with
-- the delete key, copies one with kb-custom-1, and keeps a most-recently-used
-- order in a closure for as long as the mode lives. Install as
-- ~/.config/sofi/scripts/bookmarks.lua and do NOT give it the execute bit --
-- that is what tells sofi to read it rather than run it. Then:
--     sofi -show bookmarks

local FILE = (os.getenv("XDG_CONFIG_HOME") or (os.getenv("HOME") .. "/.config"))
    .. "/sofi/bookmarks"

-- Function purpose: read "name<TAB>url" lines into records. Done at the chunk's
-- top level, which runs once per mode init, because re-reading the file on every
-- keystroke would buy nothing -- the entry list is only rebuilt when sofi asks.
local function load_file()
  local out = {}
  local fh = io.open(FILE, "r")
  if fh == nil then
    return out, string.format("no bookmarks file at %s", FILE)
  end
  for line in fh:lines() do
    if not line:match("^%s*#") and line:match("%S") then
      local name, url = line:match("^(.-)\t+(%S+)%s*$")
      if name == nil then
        url = line:match("^%s*(%S+)%s*$")
        name = url
      end
      out[#out + 1] = { name = name, url = url }
    end
  end
  fh:close()
  return out, nil
end

local marks, load_error = load_file()
local used = {}   -- url -> launch count, survives between calls
local hidden = {} -- url -> true, hidden by the delete key this session

-- Function purpose: most-used first, then the file's own order. A launcher wants
-- that and the file cannot express it.
local function ordered()
  local out = {}
  for i, m in ipairs(marks) do
    if not hidden[m.url] then
      out[#out + 1] = { mark = m, rank = i }
    end
  end
  table.sort(out, function(a, b)
    local ua, ub = used[a.mark.url] or 0, used[b.mark.url] or 0
    if ua ~= ub then
      return ua > ub
    end
    return a.rank < b.rank
  end)
  return out
end

-- Function purpose: one bookmark as the row table sofi reads. `entry` is what
-- gets matched and what comes back in state.entry; `info` is the payload the row
-- carries without being searchable or shown.
local function row(m)
  local n = used[m.url] or 0
  return {
    entry = m.name,
    display = n > 0
        and string.format("%s  <span size='small' alpha='60%%'>(%d)</span>",
                          sofi.markup_escape(m.name), n)
        or sofi.markup_escape(m.name),
    icon = { "text-html", "applications-internet" },
    meta = m.url,
    info = m.url,
    active = n > 0,
  }
end

-- Function purpose: the whole list, header options included. The array part is
-- the rows; the string keys are the header options, which are the same options
-- the script protocol writes as "\0key\x1fvalue" lines, spelled the same way.
local function build(message)
  local list = {
    prompt = "bookmarks",
    message = message or load_error
        or "<b>Return</b> open   <b>Shift+Delete</b> hide   <b>Alt+1</b> copy",
    ["markup-rows"] = true,
    ["use-hot-keys"] = true,
    ["keep-selection"] = true,
  }
  for _, o in ipairs(ordered()) do
    list[#list + 1] = row(o.mark)
  end
  if #list == 0 then
    list[1] = { entry = "no bookmarks", nonselectable = true, permanent = true }
  end
  return list
end

return {
  -- state.retv carries the same numbers SOFI_RETV does: 0 the first list, 1 a
  -- selected row, 2 custom input, 3 the delete key, 10..28 kb-custom-1..19.
  entries = function(state)
    if state.retv == 1 then
      local url = state.info
      if url ~= nil then
        used[url] = (used[url] or 0) + 1
        sofi.exec(string.format("xdg-open %s", sofi.shell_quote(url)))
      end
      return nil -- no list: sofi closes, as a script printing nothing does

    elseif state.retv == 2 then
      if state.input ~= nil and state.input:match("%S") then
        sofi.exec(string.format("xdg-open %s", sofi.shell_quote(state.input)))
      end
      return nil

    elseif state.retv == 3 then
      if state.info ~= nil then
        hidden[state.info] = true
        sofi.log("debug", string.format("hiding %s", state.info))
      end
      return build("hidden for this session")

    elseif state.retv == 10 then
      if state.info ~= nil then
        -- wl-copy takes the text as an argument, so there is no second shell to
        -- quote for -- nesting `sh -c '...'` around an already-quoted URL would
        -- end the outer quote at the first inner one.
        sofi.exec("wl-copy -- " .. sofi.shell_quote(state.info))
      end
      return nil
    end

    return build()
  end,
}
