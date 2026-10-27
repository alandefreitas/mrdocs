-- A transform that runs after the brief-filler transform of the stats
-- plugin. Plugins load before scripts, so by the time this function runs
-- the placeholder brief is already in the corpus, and it is the `text` the
-- configuration gives the plugin's `brief-filler` transform. The function
-- appends the `suffix` of its own `transform-options.brief-checker` block
-- to it. A symbol without the placeholder is left alone, so the suffix
-- shows up in the output only if the plugin's transform ran first, with
-- its own options.

mrdocs.register_transform("brief-checker", function(ctx)
    for _, sym in ipairs(ctx.corpus.symbols) do
        local doc = sym.doc
        if sym.kind == "function" and doc and doc.brief then
            local text = doc.brief.children[1]
            if text and text.literal == "Pending" then
                text.literal = "Pending, " .. ctx.params.suffix .. "."
            end
        end
    end
end)
