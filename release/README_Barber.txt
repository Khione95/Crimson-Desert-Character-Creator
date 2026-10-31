Character Creator Enhanced - Barber 9.1.21 - Crimson Desert
by Khione

Every option of Character Creator in the game's own barber screen, with the
game's own tabs, lists, sliders and preview, for Kliff, Damiane and Oongka.
Your choices are normal barber changes: the game keeps them in your save.
There is no extra menu and there are no hotkeys.

There are two editions; install one of them:
  - Barber (this one, recommended): changes are made at the game's barber.
  - Anywhere: an editor of its own opens anywhere with F6 / F7 / F8.
Every other part (gliding, two-handed swords, Equip All Armor) is the same.


INSTALL
-------
First remove the previous version:
  - uninstall the old "Character Creator" package (or the other edition) in
    your mod manager
  - delete "Character Creator.asi" from <game>\bin64 if it is still there

With DMM (recommended, DMM 4.5 or newer)
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
  Gender and Race  male or female; Human, Orc, Dwarf or Goblin
  Head             every head of the character's gender, from all races;
                   Face Details (features of a second head mixed in);
                   Face Shape (24 sliders); face scars and paint
  Body             the bodies of the character's race and gender, skin colour,
                   skin sliders; Body Shape (19 sliders) and Height; body scars
                   and paint
  Hair             every style of every race and gender, colour, lengths, shine,
                   hair tips and inner hair colours
  Beard            every beard for men, colour, length
  Eyebrows         colour, length, brow makeup
  Eyes             21 eye colours, iris and pupil sliders, eyelash colour
  Tattoos          face and body: type, colour, opacity, position, rotation, size
  Makeup           lips, eyeliner, eyeshadow with glitter, blush, highlighter
                   and contour

Pictures in the lists are drawn from each option's own model on one mannequin.
The words of the new tabs are in English; the game's own stay in your language.


GENDER AND RACE
---------------
The skeleton is chosen when the game starts, so these need a restart:
  1. Choose at the barber and press Save there.
  2. Save the game, then restart it and load that save.
  3. Press Save at the barber once more. Until then the barber and shop previews
     flash the old look for a moment, as they start from the look that is stored.
Body, head and beard are changed to ones that fit. A woman has the female
animations, gliding and two-handed sword fixes.

A save with no choice stored keeps the last one chosen. A save that has one
applies it at the next restart, as the game has already chosen the skeleton
when you load a save.


GOOD TO KNOW
------------
- Height shows at once, in the preview too. Until you restart, arms and shoulders
  look off while moving; after the restart they are right.
- A new head can show dark patches on the skin until you press Save and restart:
  the skin is laid out for the head the character was built with.
- Eye colour: the head is swapped for about a second while the eyes reload. It
  flickers, and that is normal.
- Face and Body Shape show a moment after you stop moving a slider. 50 is the
  head's or body's own shape; the slider goes from 0 to 100, not below zero.
- "Others" is greyed while a list is on its first option ("none"), as the game
  does for its own tabs.
- Heads of other races were made for their own race's body, so some may not sit
  perfectly at the neck.
- After a game update the new tabs switch off, and the barber keeps its own,
  until an updated version is out.
- This edition replaces the game's decorationparam_*.xml,
  customizationtexturepalette.xml and cd_item_icon.xml: other mods replacing those
  files don't work together with it.
- Gliding and two-handed swords: if another mod handles gliding, set Glide = 0
  under [Fixes] in <game>\bin64\CharacterCreator.ini (added there on the next
  start; DMM's ASI config can edit it) and restart the game. TwoHandGrip = 0 there
  switches the two-handed grip fix off (sevnupp's Female Longsword Fix,
  nexusmods.com/crimsondesert/mods/3477 - thank you!).
- Coming from the Anywhere edition: its look, gender, race and height are not used
  by this edition. Its files stay in <game>\bin64\CharacterCreator for when you go
  back.
- Uninstalling: saves remember body, head, hair and beard by their number in the
  mod's lists. Without the mod, a character may pick up a different one; a barber
  visit sets it again. The new sliders' values stay in the save and should do
  nothing without the mod.
- CharacterCreator.asi disappeared or flagged as a virus? Your antivirus
  removed it by mistake - a plugin that hooks into the game looks suspicious
  to them. Restore it (Windows Security > Virus & threat protection >
  Protection history) and add exclusions for both the game folder
  (...\steamapps\common\Crimson Desert) and your DMM folder: DMM keeps its
  own copy of the .asi in its mods folder and copies it into the game on every
  Apply. (Windows Security > Virus & threat protection > Manage settings >
  Exclusions > Add an exclusion > Folder.)


FEMALE ARMOR FIT (OPTIONAL, SEPARATE DOWNLOAD)
----------------------------------------------
Over 1000 male armor parts reshaped for a woman's body (bust, waist, shoulders,
arms and sleeves), used only while a woman wears them - men and NPCs keep the
original look. Import "Female Armor Fit" in DMM next to Character Creator and
apply. Don't combine it with mods that replace the game's part table (e.g. Cloak
Remover) or swap the same armor for women (e.g. Female Armor Module). Made for the
current game version: after a game update, disable it until an updated version is
out.


PROBLEMS
--------
Please include <game>\bin64\CharacterCreator\CharacterCreator.log with a bug
report. If the game crashes a few seconds after a character appears (right
after the log says "head from the load, without its face shape - rebuilding
it"), write "faceshape" into a text file called disable.txt in that folder:
the face shape fix (a quick head swap) is skipped. Writing "barbertabs" there
gives you the game's own barber tabs again.


CREDITS
-------
  Slinky - CharacterCustomizationExtender, whose source and parameter map
           helped a lot
  MinHook by Tsuda Kageyu (see THIRD_PARTY.txt)
