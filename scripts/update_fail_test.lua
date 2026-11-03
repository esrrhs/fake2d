-- Exit-code gate: update() always errors so Engine::Run must return non-zero.
--
--   ./build/bin/fake2d_hello --headless --interp --entry scripts/update_fail_test.lua --frames 3
--   expect: prints "fake2d: update() failed: ..." and exits != 0
--
-- The main loop keeps running after the first failure (screenshots / frame
-- budgets still complete); only the process exit status changes.

function update(dt)
    error("intentional update failure for exit-code gate")
end
