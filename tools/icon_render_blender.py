"""Blender side of the option icons (icon_meshes.py writes what it reads).

Every icon: white mannequin, black hair, one camera and one set of lights, on a
transparent background, in the style of the game's barber icons.

Usage: blender -b -P icon_render_blender.py -- <jobs.json>
jobs.json: {"meshes": dir, "threads": n, "samples": n, "size": n, "jobs": [...]}
A job: {"out": png, "view": "face", "skin": [models], "hair": [models],
        "center": mannequin's head centre, "scale": its head height / the reference's}
The body view: "center" the body's middle, "height" its height.
"decal" (optional): {"model", "png", "colour"}: paint on that model.
"""
import json
import math
import os
import sys

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector

# Camera: looks at target from yaw degrees off the face (the game's face looks
# along -z), pitch above it, distance in metres, lens in mm.
VIEWS = {
    # One camera for all that is on the head: hair, beards, heads, scars, makeup.
    'face': {'target': (0.0, 1.680, -0.100), 'yaw': 15, 'pitch': 3, 'distance': 0.97, 'lens': 85, 'tilt': 10, 'drop': 0.04},
    'body': {'target': (0.0, 0.0, 0.0), 'yaw': 0, 'pitch': 0, 'distance': 0.0, 'lens': 85},
    # Makeup the game has no picture for, framed as its pictures of the same tab are.
    'makeup_face': {'target': (0.0, 1.6950, -0.1105), 'yaw': 0, 'pitch': 0, 'distance': 0.43, 'lens': 85},
    'makeup_eye': {'target': (0.0, 1.6875, -0.1105), 'yaw': 0, 'pitch': 0, 'distance': 0.135, 'lens': 85},
}
# The game's busts stand relaxed and the head leans forward; a model's bind pose
# has the shoulders up and the head straight. tilt: the head and all on it, in
# degrees, about the base of the neck. drop: the shoulders down, in metres.
NECK_BELOW_CENTER = 0.13
NECK_BEHIND_CENTER = 0.05


def world(p):
    """Game space (y up, face along -z) to Blender's (z up, face along -y)."""
    return Vector((p[0], p[2], p[1]))


def skin_material(png='', meshes='', cache={}):
    if png in cache:
        return cache[png]
    m = bpy.data.materials.new('skin ' + png)
    m.use_nodes = True
    n = m.node_tree
    b = n.nodes['Principled BSDF']
    b.inputs['Base Color'].default_value = (0.72, 0.72, 0.73, 1)
    b.inputs['Roughness'].default_value = 0.55
    b.inputs['Specular IOR Level'].default_value = 0.3
    if png:
        tex = n.nodes.new('ShaderNodeTexImage')
        tex.image = bpy.data.images.load(os.path.join(meshes, png))
        tex.image.colorspace_settings.name = 'Non-Color'
        bump = n.nodes.new('ShaderNodeNormalMap')
        bump.uv_map = 'uv'
        bump.inputs['Strength'].default_value = 0.8
        n.links.new(tex.outputs['Color'], bump.inputs['Color'])
        n.links.new(bump.outputs['Normal'], b.inputs['Normal'])
    cache[png] = m
    return m


def hair_material(png, meshes, cache={}):
    if png in cache:
        return cache[png]
    m = bpy.data.materials.new(png)
    m.use_nodes = True
    n = m.node_tree
    b = n.nodes['Principled BSDF']
    b.inputs['Base Color'].default_value = (0.008, 0.008, 0.009, 1)
    b.inputs['Roughness'].default_value = 0.3
    b.inputs['Specular IOR Level'].default_value = 0.8
    cache[png] = m
    if not png:
        return m    # a part of a hairstyle with no texture: solid
    tex = n.nodes.new('ShaderNodeTexImage')
    tex.image = bpy.data.images.load(os.path.join(meshes, png))
    tex.image.colorspace_settings.name = 'Non-Color'
    # Thicker than the texture: the game's icons have solid black hair, no skin through it.
    ramp = n.nodes.new('ShaderNodeMapRange')
    ramp.inputs['From Min'].default_value = 0.08
    ramp.inputs['From Max'].default_value = 0.55
    n.links.new(tex.outputs['Alpha'], ramp.inputs['Value'])
    n.links.new(ramp.outputs['Result'], b.inputs['Alpha'])
    m.surface_render_method = 'BLENDED'
    cache[png] = m
    return m


def bend_at_neck(points, neck, degrees):
    """The body's model holds the skull above the face: what is above the neck
    leans with the head, bending over the neck's height."""
    t = np.clip((points[:, 2] - (neck.z - 0.04)) / 0.06, 0, 1)
    angle = np.radians(degrees) * t * t * (3 - 2 * t)
    y, z = points[:, 1] - neck.y, points[:, 2] - neck.z
    points[:, 1] = neck.y + y * np.cos(angle) - z * np.sin(angle)
    points[:, 2] = neck.z + y * np.sin(angle) + z * np.cos(angle)


def decal_material(normal, decal, meshes):
    """The mannequin's skin with a tattoo, scar, dirt or makeup painted on it."""
    m = skin_material(normal, meshes, {}).copy()
    n = m.node_tree
    b = n.nodes['Principled BSDF']
    tex = n.nodes.new('ShaderNodeTexImage')
    tex.image = bpy.data.images.load(os.path.join(meshes, decal['png']))
    tex.image.colorspace_settings.name = 'Non-Color'
    mix = n.nodes.new('ShaderNodeMix')
    mix.data_type = 'RGBA'
    mix.inputs['A'].default_value = b.inputs['Base Color'].default_value
    mix.inputs['B'].default_value = (*decal['colour'], 1)
    n.links.new(tex.outputs['Color'], mix.inputs['Factor'])
    n.links.new(mix.outputs['Result'], b.inputs['Base Color'])
    return m


def add_part(name, meshes, made, drop=0.0, neck=None, tilt=0.0, decal=None):
    subs = json.load(open(os.path.join(meshes, name.lower() + '.json')))
    data = np.load(os.path.join(meshes, name.lower() + '.npz'))
    for i, sub in enumerate(subs):
        if 'eyecover' in (sub['shader'] + sub['name']).lower():
            continue
        tris = data[f'tris{i}']
        if not len(tris):
            continue
        pos = data[f'pos{i}']
        points = np.stack([pos[:, 0], pos[:, 2], pos[:, 1]], 1)
        if drop:
            # Below the neck only: above it the body's model meets the head.
            away = np.clip((np.abs(points[:, 0]) - 0.045) / 0.105, 0, 1)
            below = np.clip((neck.z - 0.04 - points[:, 2]) / 0.03, 0, 1)
            points[:, 2] -= drop * away * away * (3 - 2 * away) * below
        if tilt:
            bend_at_neck(points, neck, tilt)
        normals = data[f'normal{i}']
        normals = np.stack([normals[:, 0], normals[:, 2], normals[:, 1]], 1)
        mesh = bpy.data.meshes.new(sub['name'])
        # The swap of y and z mirrors the model: the triangles turn over with it.
        mesh.from_pydata(points.tolist(), [], tris[:, ::-1].tolist())
        uv = data[f'uv{i}']
        layer = mesh.uv_layers.new(name='uv')
        loops = np.zeros(len(mesh.loops), dtype=np.int32)
        mesh.loops.foreach_get('vertex_index', loops)
        layer.data.foreach_set('uv', np.stack([uv[loops, 0], 1 - uv[loops, 1]], 1).ravel())
        mesh.polygons.foreach_set('use_smooth', [True] * len(mesh.polygons))
        if sub['texture']:
            mesh.normals_split_custom_set_from_vertices(normals.tolist())
        else:
            # Skin: from the shape. The stored normals of a head's scalp lean
            # away (the game hides it under hair) and leave a band on a bald one.
            b = bmesh.new()
            b.from_mesh(mesh)
            bmesh.ops.remove_doubles(b, verts=b.verts, dist=0.00005)
            b.to_mesh(mesh)
            b.free()
        mesh.update()
        if sub['texture'] or sub.get('hair'):
            mesh.materials.append(hair_material(sub['texture'], meshes))
        elif decal and decal['model'] == name.lower():
            mesh.materials.append(decal_material(sub.get('normal', ''), decal, meshes))
        else:
            mesh.materials.append(skin_material(sub.get('normal', ''), meshes))
        obj = bpy.data.objects.new(sub['name'], mesh)
        bpy.context.collection.objects.link(obj)
        made.append(obj)


def set_view(camera, rig, job, references):
    v = VIEWS[job['view']]
    center = world(job['center'])
    if job['view'] == 'body':
        target = center
        distance = job['height'] * 1.08 * v['lens'] / 36
        rig.scale = (2, 2, 2)
        power = 3.2
    else:
        scale = job.get('scale', 1)
        # The views were set on one head: others are framed by the difference.
        target = center + (world(v['target']) - world(references[job['view']])) * scale
        distance = v['distance'] * scale
        rig.scale = (1, 1, 1)
        power = 1
    rig.location = center
    for child in rig.children:
        child.data.energy = child['energy'] * power
    yaw, pitch = math.radians(v['yaw']), math.radians(v['pitch'])
    # Off the face (-y in Blender), towards the viewer's left of her.
    direction = Vector((math.sin(yaw) * math.cos(pitch), -math.cos(yaw) * math.cos(pitch), math.sin(pitch)))
    camera.location = target + direction * distance
    camera.rotation_euler = (target - camera.location).to_track_quat('-Z', 'Y').to_euler()
    camera.data.lens = v['lens']


def area(name, rig, offset, power, size):
    light = bpy.data.lights.new(name, 'AREA')
    light.size = size
    obj = bpy.data.objects.new(name, light)
    bpy.context.collection.objects.link(obj)
    obj['energy'] = power
    obj.parent = rig
    obj.location = offset
    obj.rotation_euler = (-Vector(offset)).to_track_quat('-Z', 'Y').to_euler()


def main():
    config = json.load(open(sys.argv[sys.argv.index('--') + 1]))
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.engine = 'CYCLES'
    scene.cycles.device = 'CPU'
    scene.cycles.samples = config.get('samples', 48)
    scene.cycles.use_denoising = True
    scene.cycles.transparent_max_bounces = 48
    scene.cycles.max_bounces = 6
    scene.render.threads_mode = 'FIXED'
    scene.render.threads = config.get('threads', 6)
    scene.render.film_transparent = True
    scene.render.resolution_x = scene.render.resolution_y = config.get('size', 256)
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_mode = 'RGBA'
    scene.view_settings.view_transform = 'Standard'
    scene.world = bpy.data.worlds.new('world')
    scene.world.use_nodes = True
    scene.world.node_tree.nodes['Background'].inputs['Color'].default_value = (1, 1, 1, 1)
    scene.world.node_tree.nodes['Background'].inputs['Strength'].default_value = 0.11
    camera = bpy.data.objects.new('camera', bpy.data.cameras.new('camera'))
    bpy.context.collection.objects.link(camera)
    scene.camera = camera
    rig = bpy.data.objects.new('lights', None)
    bpy.context.collection.objects.link(rig)
    area('key', rig, (0.15, -1.3, 0.9), 21, 1.0)
    area('fill', rig, (1.2, -0.5, 0.1), 4, 1.6)
    area('rim', rig, (-0.6, 1.1, 0.7), 9, 1.0)
    for job in config['jobs']:
        made = []
        view = VIEWS.get(job.get('view'), {})
        bodies = []
        neck = world(job['center']) + Vector((0, NECK_BEHIND_CENTER, -NECK_BELOW_CENTER)) * job.get('scale', 1) if 'center' in job else None
        for name in job.get('skin', []) + job.get('hair', []):
            if '_nude_' in name:
                add_part(name, config['meshes'], bodies, view.get('drop', 0.0), neck, view.get('tilt', 0.0), job.get('decal'))
            else:
                add_part(name, config['meshes'], made, decal=job.get('decal'))
        if view.get('tilt'):
            lean = Matrix.Translation(neck) @ Matrix.Rotation(math.radians(view['tilt']), 4, 'X') @ Matrix.Translation(-neck)
            for obj in made:
                obj.matrix_world = lean
        made += bodies
        set_view(camera, rig, job, config['references'])
        scene.render.filepath = job['out']
        bpy.ops.render.render(write_still=True)
        for obj in made:
            mesh = obj.data
            bpy.data.objects.remove(obj)
            bpy.data.meshes.remove(mesh)
        print('ICON', job['out'], flush=True)


main()
