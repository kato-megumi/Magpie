#!/usr/bin/env python3
"""\
Generate temporal-optimized MagpieFX shaders.

This script converts MagpieFX HLSL shaders to a temporal-optimized version with tile-based
change detection. It skips computation for tiles where input hasn't changed from the
previous frame.

Usage:
    python magpiefx_skip_unchange_tile.py <input.hlsl> [output.hlsl]
    python magpiefx_skip_unchange_tile.py <input.hlsl> --in-place
    python magpiefx_skip_unchange_tile.py --list <list.txt> [--out-dir <dir>]
    python magpiefx_skip_unchange_tile.py --list <list.txt> --in-place

Notes:
    - By default, the generated shader includes a runtime-adjustable parameter `ForceDirtyEvery`
        (DEFAULT=60) to force all tiles dirty on frame 0 and periodically.
    - Use `--no-force-dirty` to disable this and keep the previous behavior.

List file format:
  - One shader path per line
  - Empty lines are ignored
  - Lines starting with # are treated as comments

Notes:
  - In batch mode, relative paths are resolved relative to the list file location.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys


def parse_shader(content: str) -> dict:
    """Parse shader to extract passes and other blocks."""
    result = {
        'header': '',
        'textures': [],
        'samplers': [],
        'common': '',
        'passes': []
    }

    # Split into blocks by //!
    lines = content.split('\n')
    current_block = []
    current_type = 'header'

    for line in lines:
        if line.strip().startswith('//!TEXTURE'):
            if current_block:
                if current_type == 'header':
                    result['header'] = '\n'.join(current_block)
                elif current_type == 'texture':
                    result['textures'].append('\n'.join(current_block))
                elif current_type == 'sampler':
                    result['samplers'].append('\n'.join(current_block))
                elif current_type == 'common':
                    result['common'] = '\n'.join(current_block)
                elif current_type == 'pass':
                    result['passes'].append('\n'.join(current_block))
            current_block = [line]
            current_type = 'texture'
        elif line.strip().startswith('//!SAMPLER'):
            if current_block:
                if current_type == 'header':
                    result['header'] = '\n'.join(current_block)
                elif current_type == 'texture':
                    result['textures'].append('\n'.join(current_block))
                elif current_type == 'sampler':
                    result['samplers'].append('\n'.join(current_block))
                elif current_type == 'common':
                    result['common'] = '\n'.join(current_block)
                elif current_type == 'pass':
                    result['passes'].append('\n'.join(current_block))
            current_block = [line]
            current_type = 'sampler'
        elif line.strip().startswith('//!COMMON'):
            if current_block:
                if current_type == 'header':
                    result['header'] = '\n'.join(current_block)
                elif current_type == 'texture':
                    result['textures'].append('\n'.join(current_block))
                elif current_type == 'sampler':
                    result['samplers'].append('\n'.join(current_block))
                elif current_type == 'pass':
                    result['passes'].append('\n'.join(current_block))
            current_block = [line]
            current_type = 'common'
        elif line.strip().startswith('//!PASS'):
            if current_block:
                if current_type == 'header':
                    result['header'] = '\n'.join(current_block)
                elif current_type == 'texture':
                    result['textures'].append('\n'.join(current_block))
                elif current_type == 'sampler':
                    result['samplers'].append('\n'.join(current_block))
                elif current_type == 'common':
                    result['common'] = '\n'.join(current_block)
                elif current_type == 'pass':
                    result['passes'].append('\n'.join(current_block))
            current_block = [line]
            current_type = 'pass'
        else:
            current_block.append(line)

    # Don't forget the last block
    if current_block:
        if current_type == 'header':
            result['header'] = '\n'.join(current_block)
        elif current_type == 'texture':
            result['textures'].append('\n'.join(current_block))
        elif current_type == 'sampler':
            result['samplers'].append('\n'.join(current_block))
        elif current_type == 'common':
            result['common'] = '\n'.join(current_block)
        elif current_type == 'pass':
            result['passes'].append('\n'.join(current_block))

    return result


def _find_unique_texture_name(base_name: str, existing_names: set[str]) -> str:
    """Find a unique texture name by appending a suffix if needed."""
    if base_name not in existing_names:
        return base_name
    
    counter = 2
    while f"{base_name}{counter}" in existing_names:
        counter += 1
    return f"{base_name}{counter}"


def generate_temporal_shader(parsed: dict, tile_size: int = 16, force_dirty: bool = True) -> str:
    """Generate temporal-optimized shader."""

    output: list[str] = []
    
    # Extract existing texture names to detect conflicts
    existing_textures = set()
    texture_decl_map: dict[str, str] = {}
    texture_scale_map: dict[str, int] = {}  # Maps texture name to scale factor (1, 2, etc.)
    for tex in parsed['textures']:
        # Match texture declarations like "Texture2D textureName;"
        tex_match = re.search(r'Texture2D\s+(\w+)\s*;', tex)
        if tex_match:
            name = tex_match.group(1)
            existing_textures.add(name)
            texture_decl_map[name] = tex
            # Parse scale factor from WIDTH expression (e.g., INPUT_WIDTH * 2, INPUT_WIDTH * 4.0)
            width_match = re.search(r'//!WIDTH\s+(.+)$', tex, re.MULTILINE)
            if width_match:
                width_expr = width_match.group(1).strip()
                scale_match = re.search(r'INPUT_WIDTH\s*\*\s*(\d+\.?\d*)', width_expr)
                if scale_match:
                    texture_scale_map[name] = int(float(scale_match.group(1)))
                else:
                    texture_scale_map[name] = 1
    
    # Check for conflicts and rename if necessary
    dirty_map_name = _find_unique_texture_name('dirtyMap', existing_textures)
    dirty_map_prop_name = _find_unique_texture_name('dirtyMapProp', existing_textures)
    
    # Log conflicts
    if dirty_map_name != 'dirtyMap':
        print(f"Warning: Texture name conflict detected. Renaming 'dirtyMap' to '{dirty_map_name}'")
    if dirty_map_prop_name != 'dirtyMapProp':
        print(f"Warning: Texture name conflict detected. Renaming 'dirtyMapProp' to '{dirty_map_prop_name}'")

    # Header (modify SORT_NAME if present)
    header = parsed['header']
    if '//!SORT_NAME' in header:
        header = re.sub(r'(//!SORT_NAME\s+)(\S+)', r'\1\2_Temporal', header)
    else:
        # Add after VERSION line
        header = re.sub(r'(//!VERSION\s+\d+)', r'\1\n//!SORT_NAME Temporal', header)

    if force_dirty:
        # Ensure we have a per-frame counter available (MagpieFX provides `__frameCount` when `_DYNAMIC` is enabled)
        use_match = re.search(r'^\s*//!USE\s+(.+)$', header, re.MULTILINE)
        if use_match:
            raw_flags = use_match.group(1)
            flags = [f.strip() for f in raw_flags.split(',') if f.strip()]
            if '_DYNAMIC' not in {f.upper() for f in flags}:
                flags.append('_DYNAMIC')
                header = re.sub(r'^\s*//!USE\s+.+$', f"//!USE {', '.join(flags)}", header, flags=re.MULTILINE)
        else:
            header = re.sub(r'(//!VERSION\s+\d+)', r'\1\n//!USE _DYNAMIC', header)
    output.append(header)

    # Original textures
    for tex in parsed['textures']:
        output.append(tex)

    # NOTE: Do NOT add //!TEXTURE declaration for PREV_INPUT!
    # The EffectCompiler automatically creates PREV_INPUT at index 2.
    # Adding a declaration would create a duplicate (uninitialized) texture.
    # Just use PREV_INPUT in passes - it's always available.

    # Add dirty tile map textures
    output.append(f'''
// Dirty tile map - 1 pixel per {tile_size}x{tile_size} tile
//!TEXTURE
//!WIDTH (INPUT_WIDTH + {tile_size - 1}) / {tile_size}
//!HEIGHT (INPUT_HEIGHT + {tile_size - 1}) / {tile_size}
//!FORMAT R8_UNORM
Texture2D {dirty_map_name};

//!TEXTURE
//!WIDTH (INPUT_WIDTH + {tile_size - 1}) / {tile_size}
//!HEIGHT (INPUT_HEIGHT + {tile_size - 1}) / {tile_size}
//!FORMAT R8_UNORM
Texture2D {dirty_map_prop_name};
''')

    # Samplers
    for sam in parsed['samplers']:
        output.append(sam)

    # Add a point sampler for dirty tile detection (use unique name to avoid conflicts)
    dirty_sampler_name = _find_unique_texture_name('__dirtyDetectSam', existing_textures)
    existing_textures.add(dirty_sampler_name)
    output.append(f'''
//!SAMPLER
//!FILTER POINT
SamplerState {dirty_sampler_name};
''')

    # Common block
    if parsed['common']:
        output.append(parsed['common'])

    if force_dirty:
        output.append("""

//!PARAMETER
//!LABEL Force dirty every N frames (0=only frame 0)
//!DEFAULT 60
//!MIN 0
//!MAX 600
//!STEP 1
int ForceDirtyEvery;
""".rstrip())

    # Build conditional snippets for force-dirty logic
    p1_init = (
        "    bool forceDirtyAll = false;\n"
        "    uint __frame = __frameCount;\n"
        "    uint __forceEvery = ForceDirtyEvery;\n"
        "    if (__forceEvery == 0u) {\n"
        "        forceDirtyAll = (__frame == 0u);\n"
        "    } else {\n"
        "        forceDirtyAll = (__frame == 0u) || ((__frame % __forceEvery) == 0u);\n"
        "    }\n"
    ) if force_dirty else ""
    p1_gs_init = "forceDirtyAll ? 1u : 0u" if force_dirty else "0u"
    p1_guard = "!forceDirtyAll && " if force_dirty else ""
    p2_early = (
        "    {\n"
        "        uint __frame = __frameCount;\n"
        "        uint __forceEvery = ForceDirtyEvery;\n"
        "        if (__forceEvery == 0u) {\n"
        "            if (__frame == 0u) {\n"
        f"                {dirty_map_prop_name}[gxy] = (MF)1.0;\n"
        "                return;\n"
        "            }\n"
        "        } else if ((__frame == 0u) || ((__frame % __forceEvery) == 0u)) {\n"
        f"            {dirty_map_prop_name}[gxy] = (MF)1.0;\n"
        "            return;\n"
        "        }\n"
        "    }\n\n"
    ) if force_dirty else ""

    # Pass 1: Detect Changed Tiles
    output.append(f'''

//!PASS 1
//!DESC Detect Changed Tiles
//!IN INPUT, PREV_INPUT
//!OUT {dirty_map_name}
//!BLOCK_SIZE 1
//!NUM_THREADS {tile_size * tile_size}

groupshared uint gs_changed;

void Pass1(uint2 blockStart, uint3 threadId) {{
    uint2 tileCoord = blockStart;
{p1_init}
    if (threadId.x == 0) {{
        gs_changed = {p1_gs_init};
    }}
    GroupMemoryBarrierWithGroupSync();

    uint2 inputSize = GetInputSize();
    uint2 localPos = uint2(threadId.x % {tile_size}, threadId.x / {tile_size});
    uint2 gxy = tileCoord * {tile_size} + localPos;

    if ({p1_guard}gxy.x < inputSize.x && gxy.y < inputSize.y) {{
        float2 pt = GetInputPt();
        float2 pos = (gxy + 0.5f) * pt;
        if (any(INPUT.GatherRed({dirty_sampler_name}, pos) != PREV_INPUT.GatherRed({dirty_sampler_name}, pos)) ||
            any(INPUT.GatherGreen({dirty_sampler_name}, pos) != PREV_INPUT.GatherGreen({dirty_sampler_name}, pos)) ||
            any(INPUT.GatherBlue({dirty_sampler_name}, pos) != PREV_INPUT.GatherBlue({dirty_sampler_name}, pos))) {{
            InterlockedOr(gs_changed, 1);
        }}
    }}

    GroupMemoryBarrierWithGroupSync();

    // First thread writes result for this tile
    if (threadId.x == 0) {{
        {dirty_map_name}[tileCoord] = gs_changed > 0 ? (MF)1.0 : (MF)0.0;
    }}
}}
''')

    # Add propagation pass - expand dirty region by 1 tile for convolution overlap
    propagation_distance = 1
    output.append(f'''

//!PASS 2
//!DESC Propagate Dirty Tiles
//!IN {dirty_map_name}
//!OUT {dirty_map_prop_name}
//!BLOCK_SIZE 1
//!NUM_THREADS 1

void Pass2(uint2 blockStart, uint3 threadId) {{
    uint2 gxy = blockStart;  // One thread per {dirty_map_prop_name} pixel
    uint2 mapSize = (GetInputSize() + {tile_size - 1}) / {tile_size};

    if (gxy.x >= mapSize.x || gxy.y >= mapSize.y) {{
        return;
    }}

{p2_early}    bool isDirty = false;
    for (int dy = -{propagation_distance}; dy <= {propagation_distance}; dy++) {{
        for (int dx = -{propagation_distance}; dx <= {propagation_distance}; dx++) {{
            int2 neighborPos = int2(gxy) + int2(dx, dy);
            if (neighborPos.x >= 0 && neighborPos.x < (int)mapSize.x &&
                neighborPos.y >= 0 && neighborPos.y < (int)mapSize.y) {{
                if ({dirty_map_name}[uint2(neighborPos)] > (MF)0.5) {{
                    isDirty = true;
                }}
            }}
        }}
    }}

    {dirty_map_prop_name}[gxy] = isDirty ? (MF)1.0 : (MF)0.0;
}}
''')

    # Detect reused output textures across passes and rename when needed
    extra_textures: list[str] = []
    latest_name: dict[str, str] = {name: name for name in texture_decl_map.keys()}
    out_seen_count: dict[str, int] = {}

    def _replace_texture_names(content: str, name_map: dict[str, str]) -> str:
        updated = content
        # Replace longer names first to avoid partial replacements
        for old in sorted(name_map.keys(), key=len, reverse=True):
            new = name_map[old]
            if old != new:
                updated = re.sub(rf'\b{re.escape(old)}\b', new, updated)
        return updated

    def _get_out_names(pass_content: str) -> list[str]:
        out_match = re.search(r'^\s*//!OUT\s+(.+)$', pass_content, re.MULTILINE)
        if not out_match:
            return []
        return [n.strip() for n in out_match.group(1).split(',') if n.strip()]

    # Original passes with early-out check for clean tiles
    pass_num = 3
    for orig_pass_index, pass_content in enumerate(parsed['passes'], start=1):
        func_match = re.search(r'void\s+(Pass\d+)\s*\(', pass_content)
        if not func_match:
            output.append(pass_content)
            continue

        original_func_name = func_match.group(1)
        new_func_name = f'Pass{pass_num}'

        modified_pass = re.sub(r'//!PASS\s+\d+', f'//!PASS {pass_num}', pass_content)
        modified_pass = re.sub(rf'void\s+{original_func_name}\s*\(', f'void {new_func_name}(', modified_pass)

        out_names = _get_out_names(pass_content)
        pass_name_map = dict(latest_name)
        rename_outputs: dict[str, str] = {}

        for out_name in out_names:
            if out_name in texture_decl_map:
                out_seen_count[out_name] = out_seen_count.get(out_name, 0) + 1
                if out_seen_count[out_name] > 1:
                    new_name = _find_unique_texture_name(f"{out_name}_p{orig_pass_index}", existing_textures)
                    existing_textures.add(new_name)
                    rename_outputs[out_name] = new_name
                    print(
                        f"Warning: Output texture reuse detected for '{out_name}' in pass {orig_pass_index}. "
                        f"Renaming to '{new_name}'."
                    )

                    decl_block = texture_decl_map[out_name]
                    new_decl = re.sub(
                        rf'(Texture2D\s+){re.escape(out_name)}(\s*;)',
                        rf'\1{new_name}\2',
                        decl_block,
                        count=1
                    )
                    extra_textures.append(new_decl)
                    # Register renamed texture so scale factor lookups work
                    texture_decl_map[new_name] = new_decl
                    texture_scale_map[new_name] = texture_scale_map.get(out_name, 1)

        modified_pass = _replace_texture_names(modified_pass, pass_name_map)

        # Apply output texture renames to the function body (for textures being reused)
        if rename_outputs:
            modified_pass = _replace_texture_names(modified_pass, rename_outputs)

        if out_names:
            def _rewrite_out_line(match: re.Match) -> str:
                current_outs = [n.strip() for n in match.group(1).split(',') if n.strip()]
                rewritten = []
                for name in current_outs:
                    original_name = name
                    # Reverse-map to original if already replaced
                    for k, v in pass_name_map.items():
                        if v == name:
                            original_name = k
                            break
                    if original_name in rename_outputs:
                        rewritten.append(rename_outputs[original_name])
                    else:
                        rewritten.append(name)
                return f"//!OUT {', '.join(rewritten)}"

            modified_pass = re.sub(r'^\s*//!OUT\s+(.+)$', _rewrite_out_line, modified_pass, flags=re.MULTILINE)

        for original_name, new_name in rename_outputs.items():
            latest_name[original_name] = new_name

        is_detection_pass = dirty_map_name in modified_pass or 'PREV_INPUT' in modified_pass

        if not is_detection_pass and dirty_map_prop_name not in modified_pass:
            in_match = re.search(r'//!IN\s+(.+)$', modified_pass, re.MULTILINE)
            if in_match:
                current_inputs = in_match.group(1).strip()
                modified_pass = re.sub(
                    r'//!IN\s+.+$',
                    f'//!IN {current_inputs}, {dirty_map_prop_name}',
                    modified_pass,
                    flags=re.MULTILINE
                )

            out_match = re.search(r'//!OUT\s+(\S+)', modified_pass)
            output_tex = out_match.group(1).rstrip(',') if out_match else ''
            # Use bilinear sampling only for textures without explicit width/height
            is_output_resolution = output_tex != '' and output_tex not in texture_scale_map

            func_body_match = re.search(rf'(void\s+{new_func_name}\s*\([^)]+\)\s*\{{)', modified_pass)
            if func_body_match:
                func_header = func_body_match.group(1)
                if is_output_resolution:
                    # Map OUTPUT coordinates back to INPUT coordinates using the actual scale ratio
                    # Use bilinear sampling of dirtyMapProp to handle non-integer scale factors gracefully
                    # This ensures that if any neighboring tile is dirty, we'll process this tile
                    tile_calc = f'''float2 inputPos = blockStart * (float2)GetInputSize() / (float2)GetOutputSize();
    float2 dirtyMapSize = float2((GetInputSize() + {tile_size - 1}) / {tile_size});
    float2 dirtyUV = (inputPos / {tile_size}) / dirtyMapSize;'''
                    dirty_check = f'{dirty_map_prop_name}.SampleLevel({dirty_sampler_name}, dirtyUV, 0).r'
                else:
                    # Check if output texture has a scale factor > 1
                    scale_factor = texture_scale_map.get(output_tex, 1)
                    if scale_factor > 1:
                        # Intermediate texture at higher resolution - divide by scale factor first
                        tile_calc = f'uint2 tileCoord = (blockStart / {scale_factor}) / {tile_size};'
                    else:
                        tile_calc = f'uint2 tileCoord = blockStart / {tile_size};'
                    dirty_check = f'{dirty_map_prop_name}[tileCoord]'

                early_out_code = f'''
    // Early-out for clean tiles - skip expensive computation
    {tile_calc}
    if ({dirty_check} < (MF)0.5) {{
        return; // Tile is clean, intermediate texture already has valid data from last frame
    }}
'''
                modified_pass = modified_pass.replace(func_header, func_header + early_out_code)

        output.append(modified_pass)
        pass_num += 1

    # Append any extra texture declarations created from output renames
    if extra_textures:
        output.insert(len(parsed['textures']) + 1, '\n'.join(extra_textures))

    return '\n'.join(output)


def _iter_list_file(list_file: Path):
    for raw_line in list_file.read_text(encoding='utf-8').splitlines():
        line = raw_line.strip()
        if not line or line.startswith('#'):
            continue
        yield line


def _default_output_path(input_file: Path, suffix: str) -> Path:
    return input_file.with_name(f"{input_file.stem}{suffix}{input_file.suffix}")


def _process_one(input_path: Path, output_path: Path, tile_size: int, force_dirty: bool) -> None:
    content = input_path.read_text(encoding='utf-8')
    parsed = parse_shader(content)

    has_point_sampler = any('POINT' in s and 'sam' in s for s in parsed['samplers'])
    if not has_point_sampler:
        print(f"Warning: {input_path} has no POINT sampler named 'sam'. Change detection may not work correctly.")

    temporal_shader = generate_temporal_shader(parsed, tile_size=tile_size, force_dirty=force_dirty)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(temporal_shader, encoding='utf-8')


def _build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description='Generate a temporal-optimized MagpieFX shader (or batch-process a list).'
    )
    parser.add_argument('input', nargs='?', help='Input .hlsl shader path')
    parser.add_argument('output', nargs='?', help='Optional output .hlsl shader path (single-file mode only)')
    parser.add_argument('--list', dest='list_file', help='Path to a text file containing input shader paths')
    parser.add_argument('--out-dir', dest='out_dir', help='Output directory for batch mode (defaults to each input directory)')
    parser.add_argument('--suffix', default='_Skip', help='Suffix for auto-generated output filenames')
    parser.add_argument('--tile-size', type=int, default=8, help='Tile size in pixels used for dirty detection')
    parser.add_argument(
        '--no-force-dirty',
        action='store_true',
        help='Disable ForceDirtyEvery and keep the previous behavior (no periodic forced dirty tiles)'
    )
    parser.add_argument('--in-place', action='store_true', help='Overwrite the input shader file(s) in place')
    return parser


def main() -> None:
    parser = _build_arg_parser()
    args = parser.parse_args()

    if args.tile_size <= 0:
        parser.error('--tile-size must be > 0')

    if args.list_file and (args.input or args.output):
        parser.error('Use either single-file args (input/output) OR --list, not both.')

    if args.in_place and args.output:
        parser.error('Do not provide an explicit output path when using --in-place.')

    if args.in_place and args.out_dir:
        parser.error('Do not provide --out-dir when using --in-place (it overwrites inputs).')

    if not args.list_file and not args.input:
        print(__doc__)
        sys.exit(1)

    if args.list_file:
        list_path = Path(args.list_file)
        if not list_path.is_file():
            print(f"List file not found: {list_path}")
            sys.exit(2)

        out_dir = Path(args.out_dir) if args.out_dir else None

        inputs: list[Path] = []
        for entry in _iter_list_file(list_path):
            p = Path(entry)
            if not p.is_absolute():
                p = (list_path.parent / p).resolve()
            inputs.append(p)

        if not inputs:
            print(f"No inputs found in list: {list_path}")
            return

        failed = 0
        for input_path in inputs:
            try:
                if not input_path.is_file():
                    raise FileNotFoundError(str(input_path))

                if args.in_place:
                    output_path = input_path
                    print(f"Overwriting: {input_path}")
                else:
                    output_path = _default_output_path(input_path, args.suffix)
                    if out_dir:
                        output_path = out_dir / output_path.name

                _process_one(input_path, output_path, tile_size=args.tile_size, force_dirty=not args.no_force_dirty)
                print(f"Generated: {output_path}")
            except Exception as ex:
                failed += 1
                print(f"Failed: {input_path} ({ex})")

        if failed:
            print(f"Done with errors: {failed}/{len(inputs)} failed")
            sys.exit(3)

        print(f"Done: {len(inputs)} shaders processed")
        return

    # Single-file mode
    input_path = Path(args.input)
    if not input_path.is_file():
        print(f"Input file not found: {input_path}")
        sys.exit(2)

    if args.in_place:
        output_path = input_path
        print(f"Overwriting: {input_path}")
    elif args.output:
        output_path = Path(args.output)
    else:
        output_path = _default_output_path(input_path, args.suffix)

    _process_one(input_path, output_path, tile_size=args.tile_size, force_dirty=not args.no_force_dirty)
    print(f"Generated temporal-optimized shader: {output_path}")


if __name__ == '__main__':
    main()
