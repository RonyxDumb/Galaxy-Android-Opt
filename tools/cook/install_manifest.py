"""Prepare the installed disc's actual material inventory before launching it."""
import os

def prepare_graphics_manifest(game_root):
    path = os.path.join(game_root, 'galaxy_shader_manifest.bin')
    temporary = path + '.tmp'
    try:
        try:
            from generate_shader_manifest import scan_directory_for_models, write_manifest
        except ImportError:
            from tools.cook.generate_shader_manifest import scan_directory_for_models, write_manifest
        materials, report = scan_directory_for_models(os.path.join(game_root, 'files'))
        if not materials:
            raise ValueError('No material inventory: ' + str(report['gaps'][:3]))
        write_manifest(materials, temporary)
        os.replace(temporary, path)
    except Exception as error:
        # This is a cache optimization. Converted game data is validated
        # separately; a scanner gap must not invalidate a usable installation.
        print('[Graphics Warning] Using runtime shader preparation: %s' % error)
        try:
            os.unlink(temporary)
        except OSError:
            pass
        return None
    print('[Graphics] Prepared %d material pipelines from %d models; %d parser gaps' %
          (len(materials), report['models_parsed'], len(report['gaps'])))
    return path
