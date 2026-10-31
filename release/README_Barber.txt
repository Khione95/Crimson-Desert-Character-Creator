Character Creator Enhanced - Barber 9.1.18 - Crimson Desert
by Khione

The mod's heads, hair, beards and bodies for Kliff, Damiane and Oongka at the
game's own barber - no extra menu, no hotkeys. Visit a barber and choose as
usual; the game keeps the look in your save.

There are two editions; install one of them:
  - Barber (this one): changes are made at the game's barber.
  - Anywhere: an editor of its own opens anywhere with F6 / F7 / F8, with
    more choices (gender, race, height, eye colour, face details, tattoos,
    scars, paint). Every other part (gliding, two-handed swords, Equip All
    Armor) is the same.


INSTALL
-------
First remove the previous version:
  - uninstall the old "Character Creator" package (or the other edition) in
    your mod manager
  - delete "Character Creator.asi" from <game>\bin64 if it is still there

With DMM (recommended)
  1. Import the downloaded zip in DMM as it is (don't unzip and re-zip it).
  2. Make sure all parts are enabled: "Character Creator Enhanced - Barber /
     Character Creator Enhanced" and the Equip All Armor module in the mod
     list, and "CharacterCreator" in the ASI plugins.
  3. Click Apply. DMM installs the game files, places CharacterCreator.asi in
     bin64 and sets up the ASI loader.
  4. Start the game, load a save and visit a barber.

Manual install (another mod manager, or no .asi support)
  1. Install the game files with your mod manager as usual: the "Character
     Creator Enhanced" folder inside "Character Creator Enhanced - Barber"
     holds the 0009, 0010 and 0012 folders ("Equip All Armor.json" is a DMM
     mod: every character can wear each other's armor).
  2. Install the Ultimate ASI Loader into <game>\bin64 if you do not have it
     yet (many mods use it, usually as winmm.dll).
  3. Copy "CharacterCreator.asi" from the "Character Creator Enhanced -
     Barber" folder into <game>\bin64.
  4. Start the game, load a save and visit a barber.

On its first start the plugin creates <game>\bin64\CharacterCreator with its
data; the log is kept there too.


AT THE BARBER
-------------
  Body      the bodies of the character's own race and gender
  Head      every head of the character's gender, from all races
  Hair      every style of every race and gender
  Beard     every beard, for Kliff and Oongka (with "None")
  Colours, lengths and makeup are the barber's own.

Each head brings its own face shape, kept matching the head automatically.


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
- Heads of other races were made for their own race's body, so some may not
  sit perfectly at the neck.
- Gliding and two-handed swords: the same fixes as in the Anywhere edition.
  If another mod handles gliding, set Glide = 0 under [Fixes] in
  <game>\bin64\CharacterCreator.ini (added there on the next start; DMM's ASI
  config can edit it) and restart the game. TwoHandGrip = 0 there switches
  the two-handed grip fix off (sevnupp's Female Longsword Fix,
  nexusmods.com/crimsondesert/mods/3477 - thank you!).
- Coming from the Anywhere edition: its choices (gender, race, height, eye
  colour, the editor's look) are not used by this edition; the characters are
  themselves, with the look the barber gives them. The files stay in
  <game>\bin64\CharacterCreator for when you go back.
- Uninstalling: saves remember body, head, hair and beard by their number in
  the mod's lists. Without the mod, a character may pick up a different one;
  a barber visit sets it again.
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
report. If the game crashes a few seconds after a character appears (right
after the log says "head from the load, without its face shape - rebuilding
it"), write "faceshape" into a text file called disable.txt in that folder:
the face shape fix (a quick head swap) is skipped.


CREDITS
-------
  Slinky - CharacterCustomizationExtender, whose source and parameter map
           helped a lot
  MinHook by Tsuda Kageyu (see THIRD_PARTY.txt)
