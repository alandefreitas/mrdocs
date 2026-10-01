-- A transform that runs after the brief-filler transform of the stats
-- plugin. Plugins load before scripts, so by the time this function runs
-- the placeholder brief is already in the corpus; the function appends
-- the `suffix` of its own `transform-options.brief-checker` block to it.
-- A symbol without the placeholder is left alone, so the suffix shows up
-- in the output only if the plugin's transform ran first.

mrdocs.register_transform("brief-checker", function(ctx)
    for _, sym in ipairs(ctx.corpus.symbols) do
        local doc = sym.doc
        if sym.kind == "function" and doc and doc.brief then
            local text = doc.brief.children[1]
            if text and text.literal == "Undocumented." then
                text.literal = "Undocumented, " .. ctx.params.suffix .. "."
            end
        end
    end
end)
