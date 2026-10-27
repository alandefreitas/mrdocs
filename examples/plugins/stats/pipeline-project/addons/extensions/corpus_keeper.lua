-- A transform that keeps the corpus after the run. The values it keeps have a
-- finalizer that reads the symbols, and the script engine runs finalizers
-- when it closes at the end of the run. The tool has to keep the corpus
-- alive until then, so a run that ends with a crash fails the test. The
-- finalizer reads strings only: Lua does not finalize a value that a
-- finalizer creates while the engine closes, so a symbol it had to wrap in
-- a new value would never be released. The symbols are wrapped here, during
-- the run, instead.

local keeper

mrdocs.register_transform("corpus-keeper", function(ctx)
    local symbols = ctx.corpus.symbols
    local kept = {}
    for i = 1, #symbols do
        kept[i] = symbols[i]
    end
    keeper = setmetatable({ symbols = kept }, {
        __gc = function(self)
            for _, symbol in ipairs(self.symbols) do
                local _ = symbol.name
            end
        end
    })
end)
