# Play as any side (not original)

Start a campaign, custom or community mission playing a side other than the one it was written
for: the Emperor's Sardaukar in an Atreides mission, the Harkonnen defending against an Atreides
attack, the Fremen allied with the Atreides, ... Nothing changes unless the player picks a side.

## What Gruntmods does
Gruntmods 1.6.7 has no player-facing "play as" choice. "Play as" exists only for mission
authors: the map editor's "Play as" field writes `SideId=` into `Missions/<map>.ini`
([Basic]), and the MissionLauncher (.NET; strings `SideId`, `MySideID`, `spawn.ini`) copies it to
`spawn.ini` `[Settings] MySideID=` before it starts the game (seen: `Scenario=EO7V1`,
`MySideID=3` for a Sardaukar mission). The game then simply runs with that MySideID: the side
keeps whatever the .MIS AI block says (the editors write the AI block of the intended player with
the "enabled" byte 0) and the mission's events are written for that side. Playing the Emperor
in Gruntmods therefore means playing a community mission whose author chose SideId=3 (War of the
Landsraad – Sardaukar, already under Community here). The launcher's "house selection" tab only
picks a campaign. MissionLauncher-readme.txt and Dune2000MissionLauncherManual.pdf
(Documentation/) describe the same keys.

We keep that meaning (the human side = MySideID, `World::mySide()`) and add what the launcher
cannot do: pick the side at start, swap the computer player, and give the mission's win/lose
events a sensible meaning for the new side.

## Rules
Setup (`World(map, mis, ..., mySide, scriptSide)`, `World::setupPlayAs`, mission.cpp):
- The chosen side is the human player: its computer player is switched off (CAI +1 = 0).
- The mission's own side (`scriptSide()`, the .ini SideId) gets a computer player from its own
  .MIS AI block (every shipped .MIS stores a full parameter set for all 8 sides; only the
  "enabled" byte is 0 for the player), or the InitAI defaults when that block is empty. It is
  activated with the other computer players (ActivateAI, same RNG order).
- Houses, credits, tech levels, starting units, diplomacy, events, reinforcements, messages,
  timers: as the mission defines them. Map objects of the chosen side start Idle, those of the
  former player side Guard (the original's rule for non-player sides in single player).
- Difficulty (costs / build speed) applies to the human side, as for MySideID.

Outcome (`World::missionOutcome`, `World::checkPlayAsEnd`):
1. Mission Win / Mission Fail events (vanilla 10/11, extended ET_WIN/ET_LOSE) are the mission's
   verdict for its own side. When the chosen side and the mission's side are enemies (diplomacy
   1 either way, at the time of the event) the verdict is inverted: the Atreides failing (base
   destroyed, time out, key unit lost) is a Sardaukar win; the Atreides winning (enemies
   destroyed, survived the timer, spice quota) is a loss. Allied or neutral to the mission's side
   (e.g. the Fremen in an Atreides mission): the same verdict.
2. Elimination, checked after the events every tick (like the skirmish rules): the human side
   loses when it has no buildings (walls excluded) and no ground units (gBuildingsExist /
   gUnitsExist), and wins when no side hostile to it (diplomacy 1 either way) has any — once at
   least one hostile side was seen with objects (`playAsHostiles_`), so a mission whose enemies
   only arrive later does not end at once. Neutral sides need not be destroyed.
3. First verdict wins (gWin / gLose never both), as in the original.

Side references remapped to the mission's side (`scriptSide()`):
- Spice Harvested condition (vanilla type 8 reads MySideID's credits).
- Extended events: the "my side" object/side filters, ET_GET_MY_SIDE_ID, the spice-harvested
  style conditions (everything that called the runner's `mySide()`).
- Not remapped: Tile Revealed (the shroud is the human side's; the original player's view does
  not exist) and Reveal Map (reveals for the human). The extended "Switch my side" event is
  ignored while playing as another side (the human keeps the chosen side).

UI follows the chosen side's house automatically (sidebar art, house colours, unit voices,
radar, shroud = MySideID). Mentat speech is silent for houses 3..7 exactly as the original's
PlayMentatSound 0x46f900 (only Atreides/Harkonnen/Ordos have mentat lines). The mission music
stays the mission's own track.

Progress: a mission played as another side does not advance the campaign or community progress
(won = false for the menus); the game returns to the mission list.

## Which sides are offered
`playAsOptions(dataDir, map)` (game_data.cpp): the mission's own side first, then every side that
starts with units or buildings in the map's special layer (`game::sideObjectCounts`, the same
decoding as buildScenario), the Sandworm house (7) excluded. A side that only arrives through
reinforcement events is not offered. Missions with one side show no choice.

## UI
- Enhanced briefing page (campaign mission list, custom missions, community campaign missions):
  "Play as" choice between Back and Start mission, "Atreides - mission side", "Emperor",
  "Ordos (side 2)" when several sides share a house. Resets to the mission's side for every
  mission; nothing is saved in settings.
- Original pages: the custom / community pages (Maps & Missions tab, a community campaign's
  mission list) have an "As: <house>" button between Cancel and Start that cycles the sides.
  The original mission select page (54 campaign buttons) starts at once and has no choice.
- `--map A9V1.MAP --play-as 3` starts a mission straight away as that side.

## Saving
World: `scriptSide_` and `playAsHostiles_`; SaveMeta / GameStart: `playAsSide` (save version 6).
A loaded game recreates the World for the same start, then overwrites it.

## Tests
game_test `testPlayAs`: A9V1 played as the Sardaukar (side 3) — AI swapped, the Atreides AI
builds/moves within 6000 ticks, outcome remap and elimination rules, save/load round trip
identical; a normal start with scriptSide == mySide hashes equal to the plain constructor.
