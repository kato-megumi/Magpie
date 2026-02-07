"""
MagpieFX Effect Combiner

Combines multiple MagpieFX effects into a single effect file.
Handles:
- Input/output chaining (output of effect N becomes input of effect N+1)
- Size function adjustments (GetInputSize, GetInputPt, etc. per effect context)
- Renaming textures, parameters, and samplers to avoid conflicts
- Sequential pass renumbering
- Intermediate texture generation

Usage:
    python combine_effects.py effect1.hlsl effect2.hlsl -o combined.hlsl
    python combine_effects.py effect1.hlsl effect2.hlsl effect3.hlsl --name "Combined Effect"
"""

import re
import argparse
from pathlib import Path
from typing import List, Dict, Tuple, Optional
from dataclasses import dataclass, field


@dataclass
class EffectBlock:
    """Parsed blocks from an effect file."""
    name: str
    header: str = ""
    parameters: List[str] = field(default_factory=list)
    textures: List[str] = field(default_factory=list)
    samplers: List[str] = field(default_factory=list)
    common: List[str] = field(default_factory=list)
    passes: List[str] = field(default_factory=list)
    
    # Extracted info
    param_names: List[str] = field(default_factory=list)
    texture_names: List[str] = field(default_factory=list)
    sampler_names: List[str] = field(default_factory=list)
    uses_muladd: bool = False
    uses_fp16: bool = False


def parse_effect_file(filepath: str) -> EffectBlock:
    """Parse an effect file into blocks."""
    path = Path(filepath)
    source = path.read_text(encoding='utf-8')
    
    effect = EffectBlock(name=path.stem)
    
    # Check for USE and CAPABILITY flags
    effect.uses_muladd = bool(re.search(r'//!USE\s+.*MULADD', source, re.IGNORECASE))
    effect.uses_fp16 = bool(re.search(r'//!CAPABILITY\s+.*FP16', source, re.IGNORECASE))
    
    # Find all block starts
    block_pattern = re.compile(r'^//!(PARAMETER|TEXTURE|SAMPLER|COMMON|PASS)\b', re.MULTILINE)
    
    # Everything before the first block marker is the header
    first_match = block_pattern.search(source)
    if first_match:
        effect.header = source[:first_match.start()].strip()
    else:
        effect.header = source.strip()
        return effect
    
    # Find all block boundaries
    matches = list(block_pattern.finditer(source))
    
    for i, match in enumerate(matches):
        block_type = match.group(1).upper()
        start = match.start()
        end = matches[i + 1].start() if i + 1 < len(matches) else len(source)
        block_content = source[start:end].strip()
        
        if block_type == 'PARAMETER':
            effect.parameters.append(block_content)
            # Extract parameter name
            name_match = re.search(r'(?:float|int)\s+(\w+)\s*;', block_content)
            if name_match:
                effect.param_names.append(name_match.group(1))
        elif block_type == 'TEXTURE':
            effect.textures.append(block_content)
            # Extract texture name
            name_match = re.search(r'Texture2D\s*(?:<[^>]+>)?\s*(\w+)\s*;', block_content)
            if name_match:
                effect.texture_names.append(name_match.group(1))
        elif block_type == 'SAMPLER':
            effect.samplers.append(block_content)
            # Extract sampler name
            name_match = re.search(r'SamplerState\s+(\w+)\s*;', block_content)
            if name_match:
                effect.sampler_names.append(name_match.group(1))
        elif block_type == 'COMMON':
            effect.common.append(block_content)
        elif block_type == 'PASS':
            effect.passes.append(block_content)
    
    return effect


def rename_identifier(text: str, old_name: str, new_name: str) -> str:
    """Rename an identifier in the text, preserving word boundaries."""
    # Use word boundaries to avoid partial matches
    pattern = r'\b' + re.escape(old_name) + r'\b'
    return re.sub(pattern, new_name, text)


@dataclass
class EffectSizeInfo:
    """Track cumulative size multipliers through effect chain."""
    # Multiplier relative to original INPUT
    input_scale_x: str = "1.0"
    input_scale_y: str = "1.0"
    output_scale_x: str = "1.0"
    output_scale_y: str = "1.0"


def parse_size_expression(expr: str) -> Tuple[str, str]:
    """
    Parse size expression like 'INPUT_WIDTH * 2' into (base, multiplier).
    Returns (base_var, multiplier_expr).
    """
    expr = expr.strip()
    
    # Match patterns like: INPUT_WIDTH, INPUT_WIDTH * 2, INPUT_WIDTH * 2.5
    match = re.match(r'(INPUT_WIDTH|INPUT_HEIGHT|OUTPUT_WIDTH|OUTPUT_HEIGHT)\s*\*\s*(.+)', expr)
    if match:
        return (match.group(1), match.group(2).strip())
    
    match = re.match(r'(INPUT_WIDTH|INPUT_HEIGHT|OUTPUT_WIDTH|OUTPUT_HEIGHT)\s*/\s*(.+)', expr)
    if match:
        return (match.group(1), f"1.0 / ({match.group(2).strip()})")
    
    # Just the variable
    if expr in ('INPUT_WIDTH', 'INPUT_HEIGHT', 'OUTPUT_WIDTH', 'OUTPUT_HEIGHT'):
        return (expr, "1.0")
    
    # Complex expression - return as-is
    return (expr, "1.0")


def create_size_helper_functions(
    effect_idx: int,
    input_scale_x: str,
    input_scale_y: str, 
    output_scale_x: str,
    output_scale_y: str,
    is_first: bool,
    is_last: bool
) -> str:
    """
    Create helper functions using multipliers of the global GetInputSize/GetOutputSize.
    
    This avoids needing to bind textures just to query their dimensions.
    Sizes are computed as multiples of the REAL input/output sizes from the constant buffer.
    """
    lines = []
    lines.append(f"// Size helpers for effect {effect_idx}")
    
    # For first effect, input is just GetInputSize()
    if is_first:
        lines.append(f"uint2 __E{effect_idx}_GetInputSize() {{ return GetInputSize(); }}")
        lines.append(f"float2 __E{effect_idx}_GetInputPt() {{ return GetInputPt(); }}")
    else:
        # Input size = original input size * cumulative scale
        if input_scale_x == "1.0" and input_scale_y == "1.0":
            lines.append(f"uint2 __E{effect_idx}_GetInputSize() {{ return GetInputSize(); }}")
            lines.append(f"float2 __E{effect_idx}_GetInputPt() {{ return GetInputPt(); }}")
        else:
            lines.append(f"uint2 __E{effect_idx}_GetInputSize() {{ return uint2(GetInputSize().x * {input_scale_x}, GetInputSize().y * {input_scale_y}); }}")
            lines.append(f"float2 __E{effect_idx}_GetInputPt() {{ return float2(GetInputPt().x / {input_scale_x}, GetInputPt().y / {input_scale_y}); }}")
    
    # For last effect, output is just GetOutputSize()
    if is_last:
        lines.append(f"uint2 __E{effect_idx}_GetOutputSize() {{ return GetOutputSize(); }}")
        lines.append(f"float2 __E{effect_idx}_GetOutputPt() {{ return GetOutputPt(); }}")
    else:
        # Output size = original input size * cumulative scale up to this point
        if output_scale_x == "1.0" and output_scale_y == "1.0":
            lines.append(f"uint2 __E{effect_idx}_GetOutputSize() {{ return GetInputSize(); }}")
            lines.append(f"float2 __E{effect_idx}_GetOutputPt() {{ return GetInputPt(); }}")
        else:
            lines.append(f"uint2 __E{effect_idx}_GetOutputSize() {{ return uint2(GetInputSize().x * {output_scale_x}, GetInputSize().y * {output_scale_y}); }}")
            lines.append(f"float2 __E{effect_idx}_GetOutputPt() {{ return float2(GetInputPt().x / {output_scale_x}, GetInputPt().y / {output_scale_y}); }}")
    
    # Scale is output/input for this effect
    lines.append(f"float2 __E{effect_idx}_GetScale() {{ return float2((float)__E{effect_idx}_GetOutputSize().x / __E{effect_idx}_GetInputSize().x, (float)__E{effect_idx}_GetOutputSize().y / __E{effect_idx}_GetInputSize().y); }}")
    
    return '\n'.join(lines)


def replace_size_functions(code: str, effect_idx: int) -> str:
    """Replace GetInputSize(), GetInputPt(), etc. with effect-specific versions."""
    replacements = [
        (r'\bGetInputSize\s*\(\s*\)', f'__E{effect_idx}_GetInputSize()'),
        (r'\bGetInputPt\s*\(\s*\)', f'__E{effect_idx}_GetInputPt()'),
        (r'\bGetOutputSize\s*\(\s*\)', f'__E{effect_idx}_GetOutputSize()'),
        (r'\bGetOutputPt\s*\(\s*\)', f'__E{effect_idx}_GetOutputPt()'),
        (r'\bGetScale\s*\(\s*\)', f'__E{effect_idx}_GetScale()'),
    ]
    
    for pattern, replacement in replacements:
        code = re.sub(pattern, replacement, code)
    
    return code


def get_output_size_expr(effect: EffectBlock) -> Tuple[str, str]:
    """Extract the OUTPUT texture size expression from effect."""
    for tex_block in effect.textures:
        if 'OUTPUT' in tex_block and 'Texture2D' in tex_block:
            # Look for WIDTH and HEIGHT
            width_match = re.search(r'//!WIDTH\s+(.+?)(?:\n|$)', tex_block)
            height_match = re.search(r'//!HEIGHT\s+(.+?)(?:\n|$)', tex_block)
            if width_match and height_match:
                return (width_match.group(1).strip(), height_match.group(1).strip())
    return ("", "")


def combine_effects(
    effect_files: List[str],
    output_name: str = "Combined",
    sort_name: Optional[str] = None
) -> str:
    """
    Combine multiple effect files into a single effect.
    
    Args:
        effect_files: List of paths to effect files
        output_name: Name for the combined effect
        sort_name: Optional display name
        
    Returns:
        Combined effect source code
    """
    effects: List[EffectBlock] = []
    for filepath in effect_files:
        effects.append(parse_effect_file(filepath))
    
    if not effects:
        raise ValueError("No effects to combine")
    
    # Build the combined effect
    lines = []
    
    # Header
    lines.append("//!MAGPIE EFFECT")
    lines.append("//!VERSION 4")
    if sort_name:
        lines.append(f"//!SORT_NAME {sort_name}")
    
    # Combine USE flags
    uses_muladd = any(e.uses_muladd for e in effects)
    uses_fp16 = any(e.uses_fp16 for e in effects)
    
    if uses_muladd:
        lines.append("//!USE MulAdd")
    if uses_fp16:
        lines.append("//!CAPABILITY FP16")
    
    lines.append("")
    lines.append(f"// Combined effect: {' + '.join(e.name for e in effects)}")
    lines.append("")
    
    # Track all renamed identifiers
    param_renames: Dict[int, Dict[str, str]] = {}  # effect_idx -> {old: new}
    texture_renames: Dict[int, Dict[str, str]] = {}
    sampler_renames: Dict[int, Dict[str, str]] = {}
    
    # Parameters - rename to avoid conflicts
    lines.append("// ============ Parameters ============")
    for i, effect in enumerate(effects):
        param_renames[i] = {}
        for j, param_block in enumerate(effect.parameters):
            param_name = effect.param_names[j] if j < len(effect.param_names) else f"param{j}"
            new_name = f"_E{i}_{param_name}"
            param_renames[i][param_name] = new_name
            
            # Update the parameter block
            updated_block = param_block
            updated_block = rename_identifier(updated_block, param_name, new_name)
            
            # Update LABEL to include effect name
            label_match = re.search(r'//!LABEL\s+(.+?)(?:\n|$)', updated_block)
            if label_match:
                old_label = label_match.group(1).strip()
                new_label = f"[{effect.name}] {old_label}"
                updated_block = updated_block.replace(
                    f"//!LABEL {old_label}",
                    f"//!LABEL {new_label}"
                )
            
            lines.append("")
            lines.append(updated_block)
    
    lines.append("")
    lines.append("// ============ Textures ============")
    
    # INPUT texture (from first effect)
    lines.append("")
    lines.append("//!TEXTURE")
    lines.append("Texture2D INPUT;")
    
    # Calculate cumulative scales for each effect (needed for intermediate textures)
    effect_input_scales: Dict[int, Tuple[str, str]] = {}  # effect_idx -> (scale_x, scale_y)
    cum_scale_x = "1"
    cum_scale_y = "1"
    for i, effect in enumerate(effects):
        effect_input_scales[i] = (cum_scale_x, cum_scale_y)
        # Update cumulative scale based on this effect's output
        effect_output_size = get_output_size_expr(effect)
        if effect_output_size[0]:
            _, width_mult = parse_size_expression(effect_output_size[0])
            _, height_mult = parse_size_expression(effect_output_size[1])
            if cum_scale_x == "1":
                cum_scale_x = width_mult
            else:
                try:
                    cum_scale_x = str(float(cum_scale_x) * float(width_mult))
                except ValueError:
                    cum_scale_x = f"({cum_scale_x}) * ({width_mult})"
            if cum_scale_y == "1":
                cum_scale_y = height_mult
            else:
                try:
                    cum_scale_y = str(float(cum_scale_y) * float(height_mult))
                except ValueError:
                    cum_scale_y = f"({cum_scale_y}) * ({height_mult})"
    
    # OUTPUT texture - only specify size if the last effect specifies an output size
    # If the last effect doesn't specify output size (e.g., Bicubic_CS), leave it unspecified
    # so Magpie can set it to the actual display resolution
    last_effect_output_size = get_output_size_expr(effects[-1])
    lines.append("")
    lines.append("//!TEXTURE")
    if last_effect_output_size[0]:
        # Last effect specifies output size - use cumulative scale
        if cum_scale_x != "1":
            lines.append(f"//!WIDTH INPUT_WIDTH * {cum_scale_x}")
            lines.append(f"//!HEIGHT INPUT_HEIGHT * {cum_scale_y}")
    # else: Last effect doesn't specify output size - leave OUTPUT size unspecified
    lines.append("Texture2D OUTPUT;")
    
    # Create intermediate textures between effects
    # Use the cumulative scale calculated earlier
    intermediate_textures = []
    inter_cum_scale_x = "1"
    inter_cum_scale_y = "1"
    for i in range(len(effects) - 1):
        tex_name = f"__Inter{i}"
        intermediate_textures.append(tex_name)
        
        # Get the output size of effect i to update cumulative scale
        effect_output_size = get_output_size_expr(effects[i])
        
        if effect_output_size[0]:
            _, width_mult = parse_size_expression(effect_output_size[0])
            _, height_mult = parse_size_expression(effect_output_size[1])
            if inter_cum_scale_x == "1":
                inter_cum_scale_x = width_mult
            else:
                try:
                    inter_cum_scale_x = str(float(inter_cum_scale_x) * float(width_mult))
                except ValueError:
                    inter_cum_scale_x = f"({inter_cum_scale_x}) * ({width_mult})"
            if inter_cum_scale_y == "1":
                inter_cum_scale_y = height_mult
            else:
                try:
                    inter_cum_scale_y = str(float(inter_cum_scale_y) * float(height_mult))
                except ValueError:
                    inter_cum_scale_y = f"({inter_cum_scale_y}) * ({height_mult})"
        
        lines.append("")
        lines.append(f"//!TEXTURE")
        lines.append(f"//!FORMAT R16G16B16A16_FLOAT")
        
        if inter_cum_scale_x != "1":
            lines.append(f"//!WIDTH INPUT_WIDTH * {inter_cum_scale_x}")
            lines.append(f"//!HEIGHT INPUT_HEIGHT * {inter_cum_scale_y}")
        else:
            lines.append("//!WIDTH INPUT_WIDTH")
            lines.append("//!HEIGHT INPUT_HEIGHT")
        
        lines.append(f"Texture2D {tex_name};")
    
    # Handle other textures from each effect (excluding INPUT, OUTPUT, PREV_INPUT)
    for i, effect in enumerate(effects):
        texture_renames[i] = {}
        
        for j, tex_block in enumerate(effect.textures):
            tex_name = effect.texture_names[j] if j < len(effect.texture_names) else ""
            
            # Skip built-in textures
            if tex_name in ("INPUT", "OUTPUT", "PREV_INPUT"):
                # Map to the correct intermediate or final texture
                if tex_name == "INPUT":
                    if i == 0:
                        texture_renames[i]["INPUT"] = "INPUT"
                    else:
                        texture_renames[i]["INPUT"] = intermediate_textures[i - 1]
                elif tex_name == "OUTPUT":
                    if i == len(effects) - 1:
                        texture_renames[i]["OUTPUT"] = "OUTPUT"
                    else:
                        texture_renames[i]["OUTPUT"] = intermediate_textures[i]
                elif tex_name == "PREV_INPUT":
                    texture_renames[i]["PREV_INPUT"] = "INPUT"  # Use original INPUT
                continue
            
            # Rename non-built-in textures
            new_name = f"_E{i}_{tex_name}"
            texture_renames[i][tex_name] = new_name
            
            # Update the texture block
            updated_block = tex_block
            updated_block = rename_identifier(updated_block, tex_name, new_name)
            
            # For effects after the first, adjust size expressions
            # INPUT_WIDTH/HEIGHT in effect i refers to effect i's input, which is
            # the original INPUT scaled by the cumulative scale up to effect i
            if i > 0:
                scale_x, scale_y = effect_input_scales[i]
                if scale_x != "1":
                    # First handle INPUT_WIDTH * N (with multiplier) - must check this first
                    def adjust_width_mult(m):
                        mult = float(m.group(2))
                        try:
                            new_mult = mult * float(scale_x)
                            return f'{m.group(1)}{new_mult}'
                        except ValueError:
                            return f'{m.group(1)}({m.group(2)}) * ({scale_x})'
                    
                    updated_block = re.sub(
                        r'(//!WIDTH\s+INPUT_WIDTH\s*\*\s*)(\d+(?:\.\d+)?)',
                        adjust_width_mult,
                        updated_block
                    )
                    # Then handle plain INPUT_WIDTH (without multiplier) - use negative lookbehind to avoid matching already scaled
                    updated_block = re.sub(
                        r'(//!WIDTH\s+)INPUT_WIDTH(?!\s*\*)',
                        rf'\1INPUT_WIDTH * {scale_x}',
                        updated_block
                    )
                if scale_y != "1":
                    def adjust_height_mult(m):
                        mult = float(m.group(2))
                        try:
                            new_mult = mult * float(scale_y)
                            return f'{m.group(1)}{new_mult}'
                        except ValueError:
                            return f'{m.group(1)}({m.group(2)}) * ({scale_y})'
                    
                    updated_block = re.sub(
                        r'(//!HEIGHT\s+INPUT_HEIGHT\s*\*\s*)(\d+(?:\.\d+)?)',
                        adjust_height_mult,
                        updated_block
                    )
                    updated_block = re.sub(
                        r'(//!HEIGHT\s+)INPUT_HEIGHT(?!\s*\*)',
                        rf'\1INPUT_HEIGHT * {scale_y}',
                        updated_block
                    )
            
            lines.append("")
            lines.append(updated_block)
    
    # Samplers - rename to avoid conflicts
    lines.append("")
    lines.append("// ============ Samplers ============")
    for i, effect in enumerate(effects):
        sampler_renames[i] = {}
        for j, sam_block in enumerate(effect.samplers):
            sam_name = effect.sampler_names[j] if j < len(effect.sampler_names) else f"sam{j}"
            new_name = f"_E{i}_{sam_name}"
            sampler_renames[i][sam_name] = new_name
            
            updated_block = sam_block
            updated_block = rename_identifier(updated_block, sam_name, new_name)
            
            lines.append("")
            lines.append(updated_block)
    
    # Common blocks with size helper functions
    lines.append("")
    lines.append("// ============ Common ============")
    
    # Add size helper functions for each effect
    # Compute cumulative scale factors through the chain
    lines.append("")
    lines.append("//!COMMON")
    
    cumulative_scale_x = "1.0"
    cumulative_scale_y = "1.0"
    
    for i, effect in enumerate(effects):
        is_first = (i == 0)
        is_last = (i == len(effects) - 1)
        
        # Input scale for this effect is the cumulative scale so far
        input_scale_x = cumulative_scale_x
        input_scale_y = cumulative_scale_y
        
        # Get this effect's output size to compute its scale
        effect_output_size = get_output_size_expr(effect)
        
        if effect_output_size[0]:
            # Parse the width/height expressions to get multipliers
            _, width_mult = parse_size_expression(effect_output_size[0])
            _, height_mult = parse_size_expression(effect_output_size[1])
            
            # Update cumulative scale
            if cumulative_scale_x == "1.0":
                cumulative_scale_x = width_mult
            else:
                cumulative_scale_x = f"({cumulative_scale_x} * {width_mult})"
            
            if cumulative_scale_y == "1.0":
                cumulative_scale_y = height_mult
            else:
                cumulative_scale_y = f"({cumulative_scale_y} * {height_mult})"
        # else: scale stays the same (1:1 pass)
        
        output_scale_x = cumulative_scale_x
        output_scale_y = cumulative_scale_y
        
        lines.append(create_size_helper_functions(
            i, input_scale_x, input_scale_y,
            output_scale_x, output_scale_y,
            is_first, is_last
        ))
        lines.append("")
    
    # Add common code from each effect
    for i, effect in enumerate(effects):
        for common_block in effect.common:
            # Extract the code after //!COMMON
            code_match = re.search(r'//!COMMON\s*\n(.*)', common_block, re.DOTALL)
            if code_match:
                code = code_match.group(1).strip()
                
                # Apply all renames for this effect
                for old, new in param_renames.get(i, {}).items():
                    code = rename_identifier(code, old, new)
                for old, new in texture_renames.get(i, {}).items():
                    code = rename_identifier(code, old, new)
                for old, new in sampler_renames.get(i, {}).items():
                    code = rename_identifier(code, old, new)
                
                # Replace size functions
                code = replace_size_functions(code, i)
                
                if code:
                    lines.append(f"\n// Common from {effect.name}")
                    lines.append(code)
    
    # Passes - renumber and update references
    lines.append("")
    lines.append("// ============ Passes ============")
    
    pass_num = 1
    total_passes = sum(len(e.passes) for e in effects)
    
    for i, effect in enumerate(effects):
        # Determine input and output textures for this effect
        if i == 0:
            effect_input = "INPUT"
        else:
            effect_input = intermediate_textures[i - 1]
        
        if i == len(effects) - 1:
            effect_output = "OUTPUT"
        else:
            effect_output = intermediate_textures[i]
        
        for j, pass_block in enumerate(effect.passes):
            is_last_pass_of_effect = (j == len(effect.passes) - 1)
            is_last_pass_overall = (pass_num == total_passes)
            
            updated_block = pass_block
            
            # Update pass number
            updated_block = re.sub(r'//!PASS\s+\d+', f'//!PASS {pass_num}', updated_block)
            
            # Update IN references
            in_match = re.search(r'//!IN\s+(.+?)(?:\n|$)', updated_block)
            if in_match:
                in_textures = in_match.group(1).strip()
                new_in_textures = []
                for tex in in_textures.split(','):
                    tex = tex.strip()
                    if tex == "INPUT":
                        new_in_textures.append(effect_input)
                    elif tex in texture_renames.get(i, {}):
                        new_in_textures.append(texture_renames[i][tex])
                    else:
                        new_in_textures.append(tex)
                
                updated_block = re.sub(
                    r'//!IN\s+.+?(?=\n|$)',
                    f'//!IN {", ".join(new_in_textures)}',
                    updated_block
                )
            
            # Update OUT references
            out_match = re.search(r'//!OUT\s+(.+?)(?:\n|$)', updated_block)
            if out_match:
                out_textures = out_match.group(1).strip()
                new_out_textures = []
                for tex in out_textures.split(','):
                    tex = tex.strip()
                    if tex == "OUTPUT":
                        if is_last_pass_of_effect:
                            new_out_textures.append(effect_output)
                        else:
                            # Non-final pass outputs to intermediate
                            new_out_textures.append(texture_renames[i].get(tex, tex))
                    elif tex in texture_renames.get(i, {}):
                        new_out_textures.append(texture_renames[i][tex])
                    else:
                        new_out_textures.append(tex)
                
                updated_block = re.sub(
                    r'//!OUT\s+.+?(?=\n|$)',
                    f'//!OUT {", ".join(new_out_textures)}',
                    updated_block
                )
            
            # Update DESC to include effect name
            desc_match = re.search(r'//!DESC\s+(.+?)(?:\n|$)', updated_block)
            if desc_match:
                old_desc = desc_match.group(1).strip()
                new_desc = f"[{effect.name}] {old_desc}"
                updated_block = re.sub(
                    r'//!DESC\s+.+?(?=\n|$)',
                    f'//!DESC {new_desc}',
                    updated_block
                )
            else:
                # Add DESC if not present
                updated_block = re.sub(
                    r'(//!PASS\s+\d+)',
                    f'\\1\n//!DESC [{effect.name}] Pass {j+1}',
                    updated_block
                )
            
            # Apply all renames to the pass code
            for old, new in param_renames.get(i, {}).items():
                updated_block = rename_identifier(updated_block, old, new)
            for old, new in texture_renames.get(i, {}).items():
                updated_block = rename_identifier(updated_block, old, new)
            for old, new in sampler_renames.get(i, {}).items():
                updated_block = rename_identifier(updated_block, old, new)
            
            # Replace size functions with effect-specific versions
            updated_block = replace_size_functions(updated_block, i)
            
            # Rename the pass function
            old_func_pattern = rf'\b(Pass{j+1})\b'
            new_func_name = f'Pass{pass_num}'
            updated_block = re.sub(old_func_pattern, new_func_name, updated_block)
            
            lines.append("")
            lines.append(updated_block)
            
            pass_num += 1
    
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(
        description='Combine multiple MagpieFX effects into a single effect file.',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
Examples:
  python combine_effects.py Bilinear.hlsl ImageAdjustment.hlsl -o combined.hlsl
  python combine_effects.py effect1.hlsl effect2.hlsl effect3.hlsl --name "My Chain"
  
Notes:
  - Effects are chained in order: output of effect N becomes input of effect N+1
  - Parameters are prefixed with effect name to avoid conflicts
  - Built-in size functions (GetInputSize, etc.) are adjusted per-effect context
'''
    )
    
    parser.add_argument(
        'effects',
        nargs='+',
        help='Effect files to combine (in order)'
    )
    parser.add_argument(
        '-o', '--output',
        default='combined.hlsl',
        help='Output file path (default: combined.hlsl)'
    )
    parser.add_argument(
        '--name',
        default=None,
        help='Display name for the combined effect'
    )
    
    args = parser.parse_args()
    
    # Verify all input files exist
    for filepath in args.effects:
        if not Path(filepath).exists():
            print(f"Error: File not found: {filepath}")
            return 1
    
    try:
        combined = combine_effects(args.effects, output_name="Combined", sort_name=args.name)
        
        output_path = Path(args.output)
        output_path.write_text(combined, encoding='utf-8')
        
        print(f"Combined {len(args.effects)} effects into: {output_path}")
        print(f"Effects: {' -> '.join(Path(f).stem for f in args.effects)}")
        
    except Exception as e:
        print(f"Error: {e}")
        return 1
    
    return 0


if __name__ == '__main__':
    exit(main())
