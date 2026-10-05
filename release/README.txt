Character Creator 9.1.12 - Crimson Desert
by Khione

An in-game appearance editor for Kliff, Damiane and Oongka. Open it anywhere,
no barber needed, and see every change live.


INSTALL
-------
First remove the previous version:
  - uninstall the old "Character Creator" package in your mod manager
  - delete "Character Creator.asi" from <game>\bin64 if it is still there
  - remove "Female Animations.field.json" if you use it - the mod now switches
    gender itself

With DMM (recommended)
  1. Import the downloaded zip in DMM as it is (don't unzip and re-zip it).
  2. Make sure all parts are enabled: "Character Creator / Character Creator
     Enhanced" and the Equip All Armor module in the mod list, and
     "CharacterCreator" in the ASI plugins.
  3. Click Apply. DMM installs the game files, places CharacterCreator.asi in
     bin64 and sets up the ASI loader.
  4. Start the game, load a save and press F6.

Manual install (another mod manager, or no .asi support)
  1. Install the game files with your mod manager as usual: the "Character
     Creator Enhanced" folder inside "Character Creator" holds the 0009 and
     0012 folders ("Equip All Armor.json" is a DMM mod: every character can
     wear each other's armor).
  2. Install the Ultimate ASI Loader into <game>\bin64 if you do not have it
     yet (many mods use it, usually as winmm.dll).
  3. Copy "CharacterCreator.asi" from the "Character Creator" folder into
     <game>\bin64.
  4. Start the game, load a save and press F6.

On its first start the plugin creates <game>\bin64\CharacterCreator with its
menu data and icons; your choices and the log are kept there too.


CONTROLS
--------
  F6 / F7 / F8      open the editor for Kliff / Damiane / Oongka
                    (the same key closes it and keeps the changes;
                    other keys can be set in <game>\bin64\CharacterCreator.ini,
                    made on the first start; DMM's ASI config can edit it)
  Tab / Shift+Tab   next / previous area
  1 - 0             jump to area 1 - 10
  Q / E or [ / ]   previous / next page of an area
  Arrows or WASD    choose; on sliders Up/Down picks a value, Left/Right changes it
  Shift             steps of 10 on sliders
  R                 camera: whole body / face (the character stands left of
                    the panel; the mouse turns the camera)
  Space / Enter     keep the changes and close
  Esc               cancel everything changed since the editor was opened

Changes show on the character at once and are saved automatically, one look
per character. The editor can be opened for a character who is not with you:
the changes are applied when they appear.


WHAT YOU CAN CHANGE
-------------------
  Gender, Race     male / female; Human, Orc, Dwarf, Goblin (after a restart)
                   Kliff can also be "Female (male animations)": a female body
                   with his own animations (crow wings, two-handed swords)
  Body             body shape, skin colour (with extra skin tones), skin shine
  Height           -20% to +20% (previewed live, exact after a restart)
  Head             every head of the character's gender, from all races
  Hair             every style of every race and gender, colour, length, shine
  Beard            every beard, for everyone (with "None"), colour, length
  Eyebrow          type, colour, length
  Face / Body Tattoo   type, colour, opacity, position, rotation, size
  Eye Colour       21 iris colours
  Scars            face and body: type, colour, opacity, position, size
  Paint & Dirt     face and body: type, colour, opacity, position, size


FEMALE ARMOR FIT (OPTIONAL, SEPARATE DOWNLOAD)
----------------------------------------------
Over 800 male armor parts reshaped for a woman's body (bust, waist,
shoulders, arms and sleeves), used only while a woman wears them - men and
NPCs keep the original look. Import "Female Armor Fit" in DMM next to
Character Creator and apply. Don't combine it with mods that replace the
game's part table (e.g. Cloak Remover) or swap the same armor for women
(e.g. Female Armor Module). Made for the current game version: after a game
update, disable it until an updated version is out.


GOOD TO KNOW
------------
- Gender, race and height are applied when the game starts: choose them (in
  the main menu if you like), then restart the game - a reminder appears when
  you close the editor. Body, head and hair are switched to ones that fit
  automatically.
- Height shows at once as a preview, but until you restart, arms and shoulders
  look off while moving. After the restart the character is built at that
  height and moves normally.
- Heads of other races were made for their own race's body, so some may not
  sit perfectly at the neck.
- Lip sync: a character played as the other gender keeps their own mouth
  movements in dialogue, matching what they say.
- Gliding: each character glides their own way whatever their gender (Kliff
  with the crow wings, Damiane with her glider, Oongka with his rocket). If
  another mod handles gliding, set Glide = 0 under [Fixes] in
  <game>\bin64\CharacterCreator.ini (added there on the next start; DMM's ASI
  config can edit it) and restart the game.
- Eye colour changes at once: the head is swapped for about a second while
  the eyes reload. Each character has their own copy of the heads and eyes, so
  NPCs keep their own eye colour.
- A character the game has not given colours yet (Oongka early in the story,
  anyone who never visited a barber) gets them from the mod the first time you
  open the editor for them; colours and tattoos work a moment later. A barber
  visit does the same.
- The mod keeps each character's chosen look. Things you set in the mod win
  over the barber; things you never touched in the mod stay as the game has
  them.
- Your choices are kept in <game>\bin64\CharacterCreator: profile*.txt (looks),
  eyes*.txt (eye colours) and identity*.txt (gender, race and height). Delete them to
  go back to the game's own looks.
- Uninstalling: saves remember body, head, hair and beard by their number in
  the mod's lists. Without the mod, a character may pick up a different one;
  a barber visit sets it again.
- Makeup and eyelash length are not included: the game does not apply them to
  the player characters.
- CharacterCreator.asi disappeared or flagged as a virus? Your antivirus
  removed it by mistake - a plugin that hooks into the game looks suspicious
  to them. Restore it (Windows Security > Virus & threat protection >
  Protection history) and add exclusions for both the game folder
  (...\steamapps\common\Crimson Desert) and your DMM folder: DMM keeps its
  own copy of the .asi in its mods folder and copies it into the game on every
  Apply. (Windows Security > Virus & threat protection > Manage settings >
  Exclusions > Add an exclusion > Folder.)


PROBLEMS
--------
Please include <game>\bin64\CharacterCreator\CharacterCreator.log with a bug
report. To check whether the editor causes a crash, write "overlay" into a text
file called disable.txt in that folder (this switches the editor panel off).
If the game crashes a few seconds after a character appears (right after the
log says "head from the load, without its face shape - rebuilding it"), write
"faceshape" into disable.txt: the face shape fix (a quick head swap) is skipped.


CREDITS
-------
  Slinky - CharacterCustomizationExtender, whose source and parameter map
           helped a lot
  MinHook by Tsuda Kageyu (see THIRD_PARTY.txt)
