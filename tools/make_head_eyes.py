"""Writes head_eyes.txt: each player head prefab and the part of it that holds
its eyes (its head_sub model, or since the 2026-10-30 update for human heads
the left eye model), read from the game's head prefabs. The plugin rebuilds a
head through one with other eyes so a new eye colour loads (menu_data.cpp).

Usage: python make_head_eyes.py
"""
import os
import re

import game_files

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'head_eyes.txt')


def main():
    lines = ['# head mesh -> the part holding its eyes (generated from the game\'s head prefabs by make_head_eyes.py)']
    count = 0
    for e in sorted(game_files.entries(r'^character/bin__/prefab/1_pc/[^/]+/head/head/[^/]+\.prefab$'), key=lambda e: e['path']):
        head = e['path'].rsplit('/', 1)[1][:-len('.prefab')]
        models = [m.decode().rsplit('/', 1)[1][:-len('.pac')] for m in re.findall(rb'character/model/[ -~]+?\.pac', game_files.read(e))]
        part = next((m for m in models if '_head_sub_' in m), None) or next((m for m in models if '_eyeleft_' in m), None)
        if part:
            lines.append(f'{head} {part}')
            count += 1
    open(OUT, 'w', encoding='utf-8', newline='\n').write('\n'.join(lines) + '\n')
    print(f'{count} heads -> {OUT}')


if __name__ == '__main__':
    main()
