-- Copyright 2026 RIMES contributors. SPDX-License-Identifier: Apache-2.0
-- Four-key UNIQUE Wubi entries commit through Rime's normal selection path.
-- Unlike speller/auto_select, this cannot select a Pinyin reverse-lookup entry.
local M = {}

function M.init(env)
    env.selecting = false
    env.connection = env.engine.context.update_notifier:connect(function(ctx)
        if env.selecting or ctx:get_option("ascii_mode") then return end
        local input = ctx.input or ""
        if not input:match("^[a-y][a-y][a-y][a-y]$") or ctx.caret_pos ~= 4 then return end
        local composition = ctx.composition
        if composition:empty() then return end
        local segment = composition:back()
        if segment.start ~= 0 or segment._end ~= 4 or not segment.menu then return end
        if segment.menu:prepare(2) ~= 1 then return end
        local candidate = segment.menu:get_candidate_at(0)
        local genuine = candidate and candidate:get_genuine()
        if not genuine or (genuine.type ~= "table" and genuine.type ~= "user_table") then return end
        if genuine.start ~= 0 or genuine._end ~= 4 then return end
        env.selecting = true
        ctx:confirm_current_selection()
        env.selecting = false
    end)
end

function M.func(key, env)
    return 2 -- kNoop; normal speller, selection, and delivery retain ownership.
end

function M.fini(env)
    if env.connection then env.connection:disconnect() end
end

return M
