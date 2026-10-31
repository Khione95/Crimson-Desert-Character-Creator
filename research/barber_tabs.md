# Barber tabs: research notes

Game build 6AE58024 / 13EE0000 (2026-10-31). Everything here comes from reading the game's files and code on disk. **Nothing has been run in the game yet**, including the prototype.

## How the barber builds its tabs (read from the code)

Class `UIGamePlayControlRootBarberShop`, vtable rva 5A57C40. Function rva 1137A80 builds the parts, called from 11378E5 right after the character is set, and the results go on a list at `self+0x388` (pointer, u32 count, u32 room).

| Record | Field | Meaning |
|---|---|---|
| Part (0x38 bytes) | +0x00 u16 | Title text |
| | +0x08 | Icon name, e.g. `ItemIcon_ItemGroup_makeup_Hair` |
| | +0x10 | Sub-tabs (pointer, count, room) |
| | +0x20 | A second list, not read yet |
| | +0x30 u32 | Camera name hash: `dyeingcamera_barber`, `_facial`, `_bodytattoo` |
| Sub-tab (0x20 bytes) | +0x00 u16 | Title text |
| | +0x02 u8 | Slot in the mesh lists (hair 2, beard 3, eyebrow 6); 7 = a type chosen by a setting |
| | +0x08 | Settings: pairs of (setting key, label) |
| | +0x18 u8 | Kind: 0 type list, 1 colours, 2 sliders |

Helpers: 1148390 copies a part, 1149540 grows the list. 113F8F0 appears to decide whether a part is offered, by looking for a setting on the character.

## Why this is good news

- **Slots look generic.** The list code (1133250) only treats slot 7 specially. Others read the character's mesh table by slot number, and the choice goes through 804870(controller, slot, option), which also special-cases only 7. So Head (1), Body (0) and Face Details (5) should need no new game logic. This is what the prototype tests.
- **Every widget we need already exists:** large icon lists, colour grids, sliders, main and sub tabbars.
- **Slinky's approach doesn't carry over.** None of the patterns in that source match this build, and the screen was redesigned around this list.

## Tab icons

`ui/xml/texture/cd_item_icon.xml` (group 0012) maps icon names to DDS files in `ui/texture/icon/`. There are nine `ItemIcon_ItemGroup_makeup_*` icons. `Head` is already there, and it's what the makeup part uses. New tabs need new entries and white silhouette icons in the same style. DMM's program names `cd_item_icon`, so it may already treat that file specially; check before relying on replacing it.

## Tab text

Titles are u16 ids registered from `UI_...` names (registration near rva 104FF2B). The words are in `gamedata/stringtable/binary__/<language>/ui.paloc`, for 15 languages, stored ChaCha20-encrypted with their own compression inside. I didn't decode them yet. Two routes:

1. **Add our own keys to `ui.paloc`.** Proper, but means translating or repeating English in 15 languages. DMM's program mentions `paloc` a lot, so it may have support.
2. **Set the widget's text after the game does.** Quick, and the code already has the class `cpp-tabbar-header-text`. This is what Slinky did (`g_setWidgetText`); that pattern needs finding again on this build. The left column shows only icons, so only the sub-tabs need text.

Recommendation: route 2 first, in English.

## The list's height

`barbershopview.css`: the part list is 850 px high and each icon 93 px, which is the 9 rows the HTML states. We'd have about 15 parts. Either it scrolls by itself (the prototype will show this), or it needs a CSS change.

**Open:** the game ships both `ui/xml/gamemain/play/barbershopview.html/.css` and a compiled `ui/bin__/barbershopview_game.uibinary`. The exe names `.uibinary` once. I don't know which one it loads. If the compiled one wins, changing the layout means editing that binary format, so avoid layout changes if scrolling works.

## Face and Body Shape: the hardest part

The barber's sliders are tied to game settings, and ours aren't. Best idea so far: new settings.

- The decoration file has **184** settings, and the character keeps **250** values (`DECORATION_COUNT`, CC's own constant). That would leave 66 free.
- Face (24) + Body (19) = **43 sliders, which would fit**.
- Add them to `decorationparam_player*.xml` with our own `UIKey`s, list them in slider sub-tabs, and have CC read those values and apply them to the bones. The barber's sliders, Save and Reset would then work for free, and the values would stay in the save.

**Risks, all untested:**
- The file's comment says the indexes must have no gaps.
- 250 is an assumption I haven't checked against how the game sizes that array or stores it in saves.
- The value is a byte, and sliders go 0 to 100, so CC would map that to its -100..100.
- Saves made this way may behave oddly if the mod is removed.

**A fallback:** keep Face and Body Shape as a small CC overlay at the barber. Test the settings route on one slider before committing.

## Other tabs

| Tab | Route | Confidence |
|---|---|---|
| Head, Body, Face Details | Type list, slots 1, 0, 5 | Good, the prototype tests it |
| Eyelashes, Lips, Eyeliner, Eyeshadow, Makeup, Tattoos | Already in the game | Already there |
| Scars, Paint & Dirt | Slot 7 sub-tabs with existing settings (`faceScarType`, `bodyDirtyType`...) | Good: same shape as tattoos |
| Skin colour | Colour sub-tab on `skinColor` | Good |
| Eye colour | CC's own textures, not a setting. Needs a setting-like route, like Face Shape | Medium |
| Gender, Race, Height | Need a restart, and aren't settings | Hardest. Could use spare settings too, or stay out of the Barber edition |

**Icons in the lists:** the barber loads each option's `IconPath` as a DDS from the game files. The new renders exist only as JPEGs inside the plugin, so the Barber edition needs them written as DDS under `0012`. That adds size to the zip. The old `k_*.dds` icons still exist until then.

**Attributes:** the game's lists have `Category`, `Match`, `GroupIconPath`, `DecorationParamIndex` and `MinValue`, which the package's lists lack. Their parser is around rva 808F00–809670. Not fully read. Hair works without them, but check `GroupIconPath` and `Category` if tabs misbehave.

## Order of work

1. **Prototype result.** Do Head and Body appear, list, change the character and survive Save? Everything depends on this.
2. Text for tabs, cameras, real type/colour sub-tabs for Head, Body, Face Details, then Skin colour.
3. Scars and Paint & Dirt, the easy additions.
4. Tab icons; DDS option icons.
5. One Face Shape slider through a new setting. If it works, all 43.
6. Eye colour, then decide on Gender, Race and Height.
7. Address patterns, so the next game update doesn't turn it all off.

## Files

- Prototype: `CharacterCreator/CharacterCreator/barbertabs.cpp` (Barber edition only; it logs every part the game builds).
- The game's page, with the widgets named: `ui/xml/gamemain/play/barbershopview.html`.

## Second pass

Still nothing run in game. These are read from code and files.

**Answered**

- **The 250 settings limit is real.** `SetDecoration` (rva 803AE0) checks `index < 250` and `index < the table's count`, and the file reader (80A380) uses 250 as well. The three `decorationparam_*.xml` files all have 184 rows, indexes 0 to 183, no gaps. So new rows from 184 on keep the "no gaps" rule. **Not checked:** how a save stores the values, so loading such a save without the mod is still a risk.
- **An inert kind of setting exists.** 12 rows are `Method="Reserved" MethodValue="Reserved"`, e.g. `Index="27" UIKey="Reserved3" PartName="Head"`. New rows of this kind should do nothing to the look themselves, leaving CC to apply them. To be tested.
- **Sliders can centre on zero.** Rows go up to `Max="254"`. A slider of 0..200 with `Default="100"` gives CC its -100..100, with Reset going to the middle.
- **Tall lists should scroll.** The `VerticalList` template has `scroll-direction="vertical"` and its own scrollbar widgets. So 15 tabs in the 9-row part list, and 43 sliders in the 8-row slider list, should need **no layout edit**. To be seen in the prototype.
- **The compiled UI appears to be what loads.** rva 41E3F20 builds `%#%#%#_%#.uibinary` under `/bin__/` (`/bin_dev/` for `EditorUI`). There's a later branch I haven't read, so an HTML fallback isn't ruled out. Slinky's edited HTML may be why that approach no longer applies. With scrolling this shouldn't matter; avoid layout changes.
- **Slider code is generic.** Its bind (rva 1134570) goes through the key lookup (8031F0), then 804590 and 803C70. No setting names in it.
- **Slider labels look like free text.** For hair, the label is formatted, registered by 3A2EC0, and the id goes in the pair. If that's how it's displayed, our sliders can say anything, without language files. To be tested.
- **Language files are readable.** `ui.paloc`: "paloc", four zero bytes, u32 packed size, u32 size, then at offset 512 an LZ4 block of records: u64 category, length-prefixed key text (e.g. `22484153794960`), length-prefixed words.
- **DMM can add language entries.** Its program has `[V3] paloc`, `paloc new_record` and "needs 'key' = the category byte (7 for item name/desc...)". So tab titles could go in a DMM JSON, like Equip All Armor. Exact format still to read from DMM's docs or a sample. It's per language, so 15 languages or English everywhere.

**Corrections to the first pass**

- Only `decorationparam_example.xml` is named in the exe. `_player` and `_player_phw` aren't literal strings there, so they're chosen some other way. Add new rows to all three files.
- The set-text function isn't found yet. Two functions with the same opening as Slinky's pattern (43C8BF0, 466B550) are something else (their bodies don't match). Candidates still to check: how `0x3896780` is used near 11300DD, with a key at 711B8D8.
- `ui.paloc` has none of the words I searched as whole entries (Hair, Body, Head...). The barber's words must be in another file; find them before relying on reusing existing translations.

**Updated plan for Face and Body Shape**

Add rows 184 to 226 to the three decoration files as Reserved rows with `Min=0 Max=200 Default=100` and our `UIKey`s. Add parts "Face Shape" and "Body Shape" whose slider sub-tabs list those keys. CC reads those values each time they change and applies them to the bones with its existing code. Test with one slider first.

**Still open**

- The user's view (2026-10-31): a Barber edition change is a normal barber change, kept in the save, as the old CC's was; that is the design. One check when new settings exist: load such a save with the mod removed, since rows 184+ are a kind the old CC did not add.
- What Apply and Save do with slots 0, 1 and 5, and whether a price is charged.
- `part+0x20` (second list) and `part+0x34`.
- What `Category`, `Match` and `GroupIconPath` do in the mesh lists.
- Whether the Anywhere edition can safely share new decoration rows (it would be a shared game file).

## Third pass

Still nothing run in game.

**What the barber doesn't show yet, by setting**

Of 172 named settings, the barber's table names about 58. The other 96 (list: `settings_not_in_barber`, below) exist in the game already, so these tabs need **no new settings**, only sub-tabs:

| Tab | Settings (index) | Sub-tabs |
|---|---|---|
| Face Scars | `faceScarType` 13, position/rotation/size 14-18, opacity 19, gloss 20, colour 21 | type, colour, sliders |
| Body Scars | `bodyScarType` 132, 133-137, 138, 139, colour 140 | same |
| Face Dirt | `faceDirtyType` 72, 73-77, 78, 79, colour 80 | same |
| Body Dirt | `bodyDirtyType` 123, 124-128, 129, 130, colour 131 | same |
| Skin (Body or Head tab) | `skinColor` 22, gloss 23, brightness 24, subsurface 25 | colour, sliders |
| Tattoo placement | Face 64-68, 70; Body 115-119, 121 | extra sliders on the existing tabs |
| Hair extras | curl 3, level 4, gloss 5, two-tone 7-12, brightness 112 | extra sliders and colours on Hair |
| Beard, Eyebrow extras | beard curl 30, level 31; eyebrow length 47, curl 48, level 49 | extra sliders |
| Eyebrow makeup | 41-46 | type, colour, sliders |
| Lower eyeliner, Eyelash opacity | 90-92; 85 | type, colour, slider |
| Eyes | iris 99-104 and 111, pupil 93-98, lens 105-110 | type, colour, sliders |

So the Eye tab's tint, size and brightness can use real settings. Only CC's Eye Colour textures (the 21 swatches) stay a special case: memory says decoration values could not colour the eyes, so `irisType` should be tried but not relied on.

**Tab text: a clean route**

Read from rva 1132739: a sub-tab's u16 title goes through 3DE3F0, whose object's vtable +8 gives the text, and that goes to **1000C20(tabbar, text, index)**, which adds the tab. So one hook on 1000C20 can swap the words for our parts: no language files, no set-text function. Text encoding still to read from 1000C20. This replaces the search for Slinky's set-text function, which isn't in this build by that shape.

The barber's words are in `localstringinfo.paloc` (category 0x0B), not `ui.paloc`, so existing translations such as "Beard" could be reused for matching tabs. Our new words would be English unless added per language.

**Cameras**

The game has `dyeingcamera`, `_barber`, `_barber_bodytattoo`, `_barber_facial`, `_custommesh`, `_horse`, `_robot`. So: Head, Face Shape, Scars and Dirt on the face use `_facial`; Body, Body Shape and body marks use `_bodytattoo`. Those are hashes of the name via rva 1545560, stored at `part+0x30`.

**Attributes**

- Decoration `Category="Face"` only sets a flag at +0x84 on the row. Our new rows should leave it out.
- Mesh list `Match` is stored as a string on the slot's description, and `Category` as a byte looked up in a table. Not yet seen where they're read. Hair works without them, so not a blocker.

**Save flow**

Its strings are only confirmation messages (`CommonModalMessage`, `%#<br/>%#`), with no price texts, unlike the dye shop's `PriceModalCost`. What it writes is still to be read; the prototype test will show whether Head and Body stay.

**Files for this pass** are in the session scratchpad only: the full list was printed from `decorationparam_player.xml` against the table function.

## Fourth pass: the full table (first test build, not run yet)

A part or group shows the first sub-tab of each kind (rva 1133250, 1134570...), so more than one type, colour or slider list needs **groups** (`part+0x20`: the top tab row; read only when the part has no sub-tabs of its own). `barbertabs.cpp` adds Head, Body and Eyes in place with the game's helpers, moves Hair's and Eyebrow's sub-tabs into a first group, adds sliders to the game's lists, and puts Head and Body first.

- **Words:** titles from 0xFE00 are answered by a hook on 3DE3F0 with an object whose text is ours. Slider labels are interned free text (149DE70), read back at 1134846.
- **Face and Body Shape:** rows 184-226, `ccFace<n>` / `ccBody<n>`, 1..100 with 50 the middle. `ReadLook` shows the game's value array is already 250 long, so these have room in the save. A value of 0 means a save from before them and counts as no change.
- **Slider range:** the widget code looks fixed at 0..100 (1134885), which is why the rows are not 0..200.
- **Slot 5:** follows the head when the barber changes it (`HookedSetMesh`). The menu's sync only fills an empty slot 5 in this edition, so Face Details stays.
- **Not in yet:** Gender, Race, Height, the 21 Eye Colour swatches.
