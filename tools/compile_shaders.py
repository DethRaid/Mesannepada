'''Compiles slang shaders for Vulkan 1.3 SPIR-V

Basic usage: compile_slang_shaders.py <input_directory> <output_directory>
'''


import os
import subprocess
import sys
from pathlib import Path


glsl_extensions = ['.vert', '.geom', '.frag', '.comp']


shader_compile_tasks = list()


def are_dependencies_modified(depfile_path, last_compile_time):    
    with open(depfile_path, 'r') as depfile:
        for dependency in depfile:
            dependency_path = Path(dependency.strip())
            if not dependency_path.exists():
                print(f"Dependency {dependency_path} does not exist, recompiling shader")
                return True
            
            if dependency_path.stat().st_mtime >= last_compile_time:
                print(f"Dependency {dependency_path} has been modified, recompiling shader")
                return True

    print("All dependencies are up-to-date")
    return False


def compile_slang_shader(input_file, output_file, include_directories, defines=[], entry_point='main'):
    depfile_path = output_file.with_suffix('.deps')
    if output_file.exists():
        # If the input file is older than the output file, don't recompile
        if input_file.stat().st_mtime < output_file.stat().st_mtime:
            return

        # If the dependencies in the dependency file are older than the output file, don't recompile
        if depfile_path.exists() and not are_dependencies_modified(depfile_path, output_file.stat().st_mtime):
            return

    command = [str(slang_exe), str(input_file), '-target', 'spirv', '-entry', entry_point, '-fvk-use-scalar-layout', '-g', '-O0', '-o', str(output_file)]
    for dir in include_directories:
        command.append('-I')
        command.append(str(dir))

    for define in defines:
        command.append('-D')
        command.append(define)

    print(f"Compiling {input_file} as Slang")
    print(f"output_file={output_file}")
    p = subprocess.Popen(command)
    shader_compile_tasks.append(p)

    # use -output-includes to get a list of included files, de-duplicate that to get a dependency list, recompile the shader if any of its dependencies have changed
    includes_command = command + ['-output-includes']    
    output = subprocess.run(includes_command, capture_output=True)

    # Collect dependencies, write to a file
    output_lines = output.stderr.splitlines()
    dependencies = set()
    for line in output_lines:
        stringline = line.decode()
        if stringline.startswith('(0): note: include'):
            dependencies.add(stringline[19:].strip()[1:-1])
    
    dependencies_filename = output_file.with_suffix('.deps')
    with open(dependencies_filename, 'w') as f:
        for dependency in dependencies:
            f.write(f"{dependency}\n")


def compile_sky_pipeline(input_file, output_file, include_directories):
    base_stem = output_file.stem

    lighting_stem = base_stem
    lighting_fs_filename = output_file.with_stem(lighting_stem).with_suffix('.frag.spv')
    compile_slang_shader(input_file, lighting_fs_filename, include_directories, ['SAH_MAIN_VIEW=1'], 'main_fs')

    # Ray traced occlusion

    rt_occlusion_stem = base_stem + "_occlusion"
    rt_occlusion_miss_filename = output_file.with_stem(rt_occlusion_stem).with_suffix(".miss.spv")
    compile_slang_shader(input_file, rt_occlusion_miss_filename, include_directories, ['SAH_RT=1', 'SAH_RT_OCCLUSION=1'], 'main_miss')
    
    # Ray traced GI

    rt_gi_stem = base_stem + "_gi"
    rt_gi_miss_filename = output_file.with_stem(rt_gi_stem).with_suffix(".miss.spv")
    compile_slang_shader(input_file, rt_gi_miss_filename, include_directories, ['SAH_RT=1', 'SAH_RT_GI=1'], 'main_miss')
    

def compile_material(input_file, output_file, include_directories):
    '''Compiles a material with all its variants
    '''

    print(f"Compiling material {input_file}")

    base_stem = output_file.stem

    # Shadow

    shadow_stem = base_stem + '_shadow'
    shadow_filename = output_file.with_stem(shadow_stem).with_suffix('.vert.spv')
    compile_slang_shader(input_file, shadow_filename, include_directories, ['SAH_DEPTH_ONLY=1', 'SAH_CSM=1', 'SAH_MULTIVIEW=1'], 'main_vs')
    
    shadow_masked_stem = base_stem + '_shadow_masked'
    shadow_masked_vs_filename = output_file.with_stem(shadow_masked_stem).with_suffix('.vert.spv')
    compile_slang_shader(input_file, shadow_masked_vs_filename, include_directories, ['SAH_DEPTH_ONLY=1', 'SAH_MASKED=1', 'SAH_CSM=1', 'SAH_MULTIVIEW=1'], 'main_vs')
    shadow_masked_fs_filename = output_file.with_stem(shadow_masked_stem).with_suffix('.frag.spv')
    compile_slang_shader(input_file, shadow_masked_fs_filename, include_directories, ['SAH_DEPTH_ONLY=1', 'SAH_MASKED=1', 'SAH_CSM=1', 'SAH_MULTIVIEW=1'], 'main_fs')

    # RSM 

    rsm_stem = base_stem + '_rsm'
    rsm_vs_filename = output_file.with_stem(rsm_stem).with_suffix('.vert.spv')
    compile_slang_shader(input_file, rsm_vs_filename, include_directories, ['SAH_RSM=1', 'SAH_MULTIVIEW=1'], 'main_vs')    
    rsm_fs_filename = output_file.with_stem(rsm_stem).with_suffix('.frag.spv')
    compile_slang_shader(input_file, rsm_fs_filename, include_directories, ['SAH_RSM=1', 'SAH_MULTIVIEW=1'], 'main_fs')
    
    rsm_masked_stem = base_stem + '_rsm_masked'
    rsm_masked_vs_filename = output_file.with_stem(rsm_masked_stem).with_suffix('.vert.spv')
    compile_slang_shader(input_file, rsm_masked_vs_filename, include_directories, ['SAH_MASKED=1', 'SAH_RSM=1', 'SAH_MULTIVIEW=1'], 'main_vs')    
    rsm_masked_fs_filename = output_file.with_stem(rsm_masked_stem).with_suffix('.frag.spv')
    compile_slang_shader(input_file, rsm_masked_fs_filename, include_directories, ['SAH_MASKED=1', 'SAH_RSM=1', 'SAH_MULTIVIEW=1'], 'main_fs')

    # Depth prepass

    prepass_stem = base_stem + '_prepass'
    prepass_filename = output_file.with_stem(prepass_stem).with_suffix('.vert.spv')
    compile_slang_shader(input_file, prepass_filename, include_directories, ['SAH_DEPTH_ONLY=1', 'SAH_MAIN_VIEW=1'], 'main_vs')
    
    prepass_masked_stem = base_stem + '_prepass_masked'
    prepass_masked_vs_filename = output_file.with_stem(prepass_masked_stem).with_suffix('.vert.spv')
    compile_slang_shader(input_file, prepass_masked_vs_filename, include_directories, ['SAH_DEPTH_ONLY=1', 'SAH_MASKED=1', 'SAH_MAIN_VIEW=1'], 'main_vs')
    prepass_masked_fs_filename = output_file.with_stem(prepass_masked_stem).with_suffix('.frag.spv')
    compile_slang_shader(input_file, prepass_masked_fs_filename, include_directories, ['SAH_DEPTH_ONLY=1', 'SAH_MASKED=1', 'SAH_MAIN_VIEW=1'], 'main_fs')

    # Gbuffer

    gbuffer_stem = base_stem + '_gbuffer'
    gbuffer_vs_filename = output_file.with_stem(gbuffer_stem).with_suffix('.vert.spv')
    compile_slang_shader(input_file, gbuffer_vs_filename, include_directories, ['SAH_MAIN_VIEW=1'], 'main_vs')    
    gbuffer_fs_filename = output_file.with_stem(gbuffer_stem).with_suffix('.frag.spv')
    compile_slang_shader(input_file, gbuffer_fs_filename, include_directories, ['SAH_MAIN_VIEW=1'], 'main_fs')
    
    gbuffer_masked_stem = base_stem + '_gbuffer_masked'
    gbuffer_masked_vs_filename = output_file.with_stem(gbuffer_masked_stem).with_suffix('.vert.spv')
    compile_slang_shader(input_file, gbuffer_masked_vs_filename, include_directories, ['SAH_MASKED=1', 'SAH_MAIN_VIEW=1'], 'main_vs')    
    gbuffer_masked_fs_filename = output_file.with_stem(gbuffer_masked_stem).with_suffix('.frag.spv')
    compile_slang_shader(input_file, gbuffer_masked_fs_filename, include_directories, ['SAH_MASKED=1', 'SAH_MAIN_VIEW=1'], 'main_fs')

    # Ray traced occlusion

    rt_occlusion_stem = base_stem + "_occlusion"
    rt_occlusion_closesthit_filename = output_file.with_stem(rt_occlusion_stem).with_suffix(".closesthit.spv")
    compile_slang_shader(input_file, rt_occlusion_closesthit_filename, include_directories, ['SAH_RT=1', 'SAH_RT_OCCLUSION=1'], 'main_closesthit')
    
    rt_occlusion_masked_stem = base_stem + "_occlusion_masked"
    rt_occlusion_masked_anyhit_filename = output_file.with_stem(rt_occlusion_masked_stem).with_suffix(".anyhit.spv")
    compile_slang_shader(input_file, rt_occlusion_masked_anyhit_filename, include_directories, ['SAH_RT=1', 'SAH_RT_OCCLUSION=1', 'SAH_MASKED=1'], 'main_anyhit')
    rt_occlusion_masked_closesthit_filename = output_file.with_stem(rt_occlusion_masked_stem).with_suffix(".closesthit.spv")
    compile_slang_shader(input_file, rt_occlusion_masked_closesthit_filename, include_directories, ['SAH_RT=1', 'SAH_RT_OCCLUSION=1', 'SAH_MASKED=1'], 'main_closesthit')

    # Ray traced GI

    rt_gi_stem = base_stem + "_gi"
    rt_gi_closesthit_filename = output_file.with_stem(rt_gi_stem).with_suffix(".closesthit.spv")
    compile_slang_shader(input_file, rt_gi_closesthit_filename, include_directories, ['SAH_RT=1', 'SAH_RT_GI=1'], 'main_closesthit')
    
    rt_gi_masked_stem = base_stem + "_gi_masked"
    rt_gi_masked_anyhit_filename = output_file.with_stem(rt_gi_masked_stem).with_suffix(".anyhit.spv")
    compile_slang_shader(input_file, rt_gi_masked_anyhit_filename, include_directories, ['SAH_RT=1', 'SAH_RT_GI=1', 'SAH_MASKED=1'], 'main_anyhit')
    rt_gi_masked_closesthit_filename = output_file.with_stem(rt_gi_masked_stem).with_suffix(".closesthit.spv")
    compile_slang_shader(input_file, rt_gi_masked_closesthit_filename, include_directories, ['SAH_RT=1', 'SAH_RT_GI=1', 'SAH_MASKED=1'], 'main_closesthit')


def compile_rt_pipeline(input_file, output_file, include_directories):
    # Compile a raygen and miss shader from the pipeline
    raygen_filename = output_file.with_suffix('.spv')
    compile_slang_shader(input_file, raygen_filename, include_directories, ['SAH_RT=1'], 'main_raygen')


def compile_glsl_shader(input_file, output_file, include_directories):
    if output_file.exists() and input_file.stat().st_mtime < output_file.stat().st_mtime:
        return
        
    command = [glslang_exe, "--target-env", "vulkan1.3", "-V", "-g", "-o", "-Od"]
    for dir in include_directories:
        command.append(f"-I{dir}")

    command.append(input_file)
    command.append("-o")
    command.append(output_file)

    print(f"Compiling {input_file} as GLSL")
    print(f"output_file={output_file}")
    p = subprocess.Popen(command)
    shader_compile_tasks.append(p)


def compile_shaders_in_path(path, root_dir, output_dir, extra_include_paths):
    include_paths = [root_dir, root_dir.parent]
    include_paths += extra_include_paths

    for child_path in path.iterdir():
        if child_path.is_dir():
            compile_shaders_in_path(child_path, root_dir, output_dir, extra_include_paths)

        else:
            relative_file_path = child_path.relative_to(root_dir)

            output_file = output_dir / relative_file_path
            if child_path.suffix == '.slang':
                output_file = output_file.with_suffix('.spv')
            else:
                output_file = output_file.with_suffix(output_file.suffix + '.spv')
            
            output_parent = output_file.parent
            output_parent.mkdir(parents=True, exist_ok=True)

            if child_path.stem == 'sky_unified':
                compile_sky_pipeline(child_path, output_file, include_paths)

            elif '.rt' in child_path.suffixes:
                compile_rt_pipeline(child_path, output_file, include_paths)
                
            elif child_path.suffix == '.slang':
                if child_path.match('materials/*'):
                    compile_material(child_path, output_file, include_paths)
                else:                    
                    compile_slang_shader(child_path, output_file, include_paths)

            elif child_path.suffix in glsl_extensions:
                compile_glsl_shader(child_path, output_file, include_paths)


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print("Usage: compile_slang_shaders.py <input_directory> <output_directory> <include_directories>")
        exit()

    input_dir = Path(sys.argv[1])
    output_dir = Path(sys.argv[2])
    extra_paths = sys.argv[3:]

    print(f"Compilling shaders from {input_dir} to {output_dir}")

    vulkan_sdk_dir = Path(os.environ["VULKAN_SDK"])

    if os.name == 'nt':
        glslang_exe = vulkan_sdk_dir / "Bin" / "glslangValidator.exe"
        slang_exe = vulkan_sdk_dir / "Bin" / "slangc.exe"
    else:
        glslang_exe = vulkan_sdk_dir / "bin" / "glslangValidator"
        slang_exe = vulkan_sdk_dir / "bin" / "slangc"


    print(f"Using GLSL compiler {glslang_exe}")
    print(f"Using Slang compiler {slang_exe}")

    # .\slangc.exe hello-world.slang -profile glsl_460 -target spirv -entry main -o hello-world.spv

    # Iterate over the input directory. For every .slang file, compile it to the output directory. Create a folder structure mirroring the folder structure in the input directory

    compile_shaders_in_path(input_dir, input_dir, output_dir, extra_paths)

    # wait for the tasks to finish
    for task in shader_compile_tasks:
        task.wait()

