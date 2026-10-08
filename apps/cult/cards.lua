-- Cult Ledger event cards, written for this game, one per line:
--   title|first choice|its effect|second choice|its effect
-- An effect is a list of changes, "c-1 m+1": c coin, f food, m cultists,
-- p prisoners, r relics, s suspicion ("heat"). Costs (a minus on anything
-- but heat) must be payable. Kept as text, split one card at a time (as
-- tables they would cost the X3 several KB more).

return [[
A curious neighbour|Bribe them|c-1|Recruit them|m+1 s+1
A lost tourist|Take them in|p+1 s+1|Point the way|
The bake sale|Run a stall|f-1 c+2|Stay home|
Market day|Buy food|c-1 f+2|Sell a relic|r-1 c+3
A police patrol|Pay a fine|c-2 s-1|Hope for the best|s+1
The antique shop|Buy a relic|c-2 r+1|Just browse|
A fresh grave|Dig it up|r+1 s+2|Not tonight|
Harvest festival|Join in|f+2|Recruit the merry|m+1 f-1
A disgruntled member|Pay them off|c-1|Let them leave|m-1 s+1
A new convert|Welcome them|m+1 f-1|Turn them away|
The charity box|Empty it|c+3 s+1|Give generously|c-1 s-1
A museum exhibit|Plan a heist|m-1 r+1 s+2|Visit politely|c-1
The fortune teller|Pay her to vouch|c-2 s-1|Walk on by|
Sacrifice night|Offer a prisoner|p-1 r+1|Light candles|f-1
A nosy journalist|Bribe them|c-2|Lock them up|p+1 s+2
A potluck supper|Host it|c-1 f+3|Bring nothing|f+1 s+1
Recruitment drive|Hand out leaflets|c-1 m+1 s+1|Word of mouth|f-1 m+1
An informant's tip|Destroy evidence|r-1 s-2|Move the altar|c-2 s-1
The pawn shop|Pawn a relic|r-1 c+4|Leave|
Hungry members|Feed them|f-2|Declare a fast|m-1
A strange artefact|Pay the finder|c-1 r+1|Report it|s-1
A willing sacrifice|Accept|p+1|Make them a member|m+1
The town fair|Run a stall|c+2 s+1|Enjoy the fair|f+1
A clever lawyer|Retain her|c-3 s-2|Show her out|
A runaway prisoner|Give chase|m-1 s-1|Let them go|p-1 s+2
Bulk candles|Stock up|c-1 r+1 f-2|Make do|
A generous donor|Accept the gift|c+2|Invite them in|m+1
The community garden|Tend it|f+2|Sell the produce|c+1 f+1
A quiet week|Keep your heads down|s-1|Hold a meeting|f-1 m+1
Old books|Study the rites|c-1 r+1 f-1|Sell them|c+2
A weary pilgrim|Give them shelter|f-1 m+1|Keep them for the rite|p+1 s+1
A collector's auction|Outbid them all|c-2 r+1|Sell a relic|r-1 c+3
A cave in the woods|Explore it|f-1 r+1|Use it as a hideout|s-1
Something in the lake|Send a diver|m-1 r+1|Fish instead|f+2
]]
