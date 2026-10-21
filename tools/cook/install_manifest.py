"""Prepare the installed disc's actual material inventory before launching it."""
import os

from generate_shader_manifest import scan_directory_for_models, write_manifest


def prepare_graphics_manifest(game_root):
    materials, report = scan_directory_for_models(os.path.join(game_root, 'files'))
    if not materials:
        raise RuntimeError('No game materials were found for graphics preparation: ' + str(report['gaps'][:3]))
    path = os.path.join(game_root, 'galaxy_shader_manifest.bin')
    write_manifest(materials, path)
    print('[Graphics] Prepared %d material pipelines from %d models; %d parser gaps' %
          (len(materials), report['models_parsed'], len(report['gaps'])))
    return path
