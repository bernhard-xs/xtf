#!/usr/bin/env python3
import os
import sys
from dataclasses import dataclass


@dataclass
class EnvInfo:
    guest: str
    arch: str
    aflags_arch: str
    cflags_arch: str
    aflags_env: str
    cflags_env: str
    link: str
    ldflags: str
    defcfg: str


@dataclass
class TestInfo:
    key: str
    directory: str
    name: str
    category: str
    envs: list[str]
    extra_cfg: str
    vary_cfg: list[str]
    vcpus: str
    local_objs: list[str]


def split_words(value: str) -> list[str]:
    return [word for word in value.split() if word]


def parse_manifest(path: str):
    globals_map: dict[str, str] = {}
    envs: dict[str, EnvInfo] = {}
    env_objects: dict[str, list[str]] = {}
    tests: list[TestInfo] = []
    objects_perbits: list[str] = []
    objects_perenv: list[str] = []

    with open(path, encoding="utf-8") as handle:
        for raw_line in handle:
            line = raw_line.rstrip("\n")
            if not line:
                continue
            fields = line.split("\t")
            kind = fields[0]

            if kind == "global":
                _, key, value = fields
                globals_map[key] = value
            elif kind == "objects":
                _, scope, value = fields
                if scope == "perbits":
                    objects_perbits = split_words(value)
                elif scope == "perenv":
                    objects_perenv = split_words(value)
            elif kind == "env":
                _, name, guest, arch, aflags_arch, cflags_arch, aflags_env, cflags_env, link, ldflags, defcfg = fields
                envs[name] = EnvInfo(
                    guest=guest,
                    arch=arch,
                    aflags_arch=aflags_arch,
                    cflags_arch=cflags_arch,
                    aflags_env=aflags_env,
                    cflags_env=cflags_env,
                    link=link,
                    ldflags=ldflags,
                    defcfg=defcfg,
                )
            elif kind == "env_objects":
                _, name, value = fields
                env_objects[name] = split_words(value)
            elif kind == "test":
                _, key, directory, name, category, env_list, extra_cfg, vary_cfg, vcpus, local_objs = fields
                tests.append(TestInfo(
                    key=key,
                    directory=directory,
                    name=name,
                    category=category,
                    envs=split_words(env_list),
                    extra_cfg=extra_cfg,
                    vary_cfg=split_words(vary_cfg),
                    vcpus=vcpus,
                    local_objs=split_words(local_objs),
                ))
            else:
                raise ValueError(f"Unexpected manifest line: {line}")

    return globals_map, envs, env_objects, tests, objects_perbits, objects_perenv


def to_rel(root: str, path: str) -> str:
    if not path:
        return ""
    if os.path.isabs(path):
        return os.path.relpath(path, root)
    return path


def source_for_obj(root: str, obj: str) -> tuple[str, str]:
    rel_obj = to_rel(root, obj)
    c_src = rel_obj[:-2] + ".c"
    s_src = rel_obj[:-2] + ".S"

    if os.path.exists(os.path.join(root, c_src)):
        return c_src, "cc"
    if os.path.exists(os.path.join(root, s_src)):
        return s_src, "as"

    raise FileNotFoundError(f"No source found for object {obj}")


def depfile_for(output: str) -> str:
    if output.endswith(".lds"):
        return output[:-4] + ".d"
    if output.endswith(".o"):
        return output[:-2] + ".d"
    raise ValueError(f"No depfile mapping for {output}")


def emit_rule(lines: list[str], name: str, command: str, *, depfile: str | None = None, deps: str | None = None):
    lines.append(f"rule {name}")
    lines.append(f"  command = {command}")
    if depfile is not None:
        lines.append(f"  depfile = {depfile}")
    if deps is not None:
        lines.append(f"  deps = {deps}")
    lines.append("")


def emit_phony(lines: list[str], output: str, inputs: list[str]):
    emit_build(lines, output, "phony", inputs)


def emit_build(
    lines: list[str],
    output: str,
    rule: str,
    inputs: list[str],
    variables: dict[str, str] | None = None,
    implicit_inputs: list[str] | None = None,
):
    line = f"build {output}: {rule}"
    if inputs:
        line += " " + " ".join(inputs)
    if implicit_inputs:
        line += " | " + " ".join(implicit_inputs)
    lines.append(line)
    if variables:
        for key, value in variables.items():
            lines.append(f"  {key} = {value}")
    lines.append("")


def build_ninja(root: str, globals_map, envs, env_objects, tests, objects_perbits, objects_perenv) -> list[str]:
    cc = globals_map["CC"]
    cpp = globals_map["CPP"]
    ld = globals_map["LD"]
    objcopy = globals_map["OBJCOPY"]
    python = globals_map["PYTHON"]
    destdir = globals_map["DESTDIR"]
    hvm64_format = globals_map["HVM64_FORMAT"]
    install_data = globals_map["INSTALL_DATA"]
    install_program = globals_map["INSTALL_PROGRAM"]
    xtfdir = globals_map["xtfdir"]
    xtftestdir = globals_map["xtftestdir"]

    install_xtfdir = f"{destdir}{xtfdir}" if destdir else xtfdir
    install_xtftestdir = f"{destdir}{xtftestdir}" if destdir else xtftestdir

    lines = ["# Autogenerated by build/gen-ninja.py. Do not edit.", "ninja_required_version = 1.3", ""]
    emit_rule(lines, "cc", f"{cc} $cflags -MT $out -MF $depfile -c $in -o $out", depfile="$depfile", deps="gcc")
    emit_rule(lines, "as", f"{cc} $aflags -MT $out -MF $depfile -c $in -o $out", depfile="$depfile", deps="gcc")
    emit_rule(lines, "cpp_lds", f"{cpp} $aflags -MT $out -MF $depfile -P $in -o $out", depfile="$depfile", deps="gcc")
    emit_rule(lines, "link", f"{ld} $ldflags $in -o $out")
    emit_rule(lines, "link_hvm64", f"{ld} $ldflags $in -o $tmpout && {objcopy} $tmpout -O {hvm64_format} $out && rm -f $tmpout")
    emit_rule(lines, "mkcfg", f"{python} build/mkcfg.py $out \"$defcfg\" \"$vcpus\" \"$extracfg\" \"$varycfg\"")
    emit_rule(lines, "mkinfo", f"{python} build/mkinfo.py $out \"$name\" \"$category\" \"$envs\" \"$variations\"")
    emit_rule(lines, "install_data", f"mkdir -p $outdir && {install_data} $in $out")
    emit_rule(lines, "install_program", f"mkdir -p $outdir && {install_program} $in $out")

    seen_outputs: set[str] = set()
    build_targets: list[str] = []
    install_targets: list[str] = []

    def emit_object(original_obj: str, output_obj: str, flags: str):
        if output_obj in seen_outputs:
            return
        source, rule = source_for_obj(root, original_obj)
        emit_build(
            lines,
            output_obj,
            rule,
            [source],
            {
                "cflags" if rule == "cc" else "aflags": flags,
                "depfile": depfile_for(output_obj),
            },
        )
        seen_outputs.add(output_obj)

    def emit_link_script(env_name: str):
        output = to_rel(root, envs[env_name].link)
        if output in seen_outputs:
            return output
        emit_build(
            lines,
            output,
            "cpp_lds",
            ["common/link.lds.S"],
            {
                "aflags": envs[env_name].aflags_env,
                "depfile": depfile_for(output),
            },
        )
        seen_outputs.add(output)
        return output

    for test in tests:
        info_output = os.path.join(test.directory, "info.json")
        emit_build(
            lines,
            info_output,
            "mkinfo",
            [os.path.join(test.directory, "Makefile"), "build/mkinfo.py"],
            {
                "name": test.name,
                "category": test.category,
                "envs": " ".join(test.envs),
                "variations": " ".join(test.vary_cfg),
            },
        )
        build_targets.append(info_output)

        install_info = os.path.join(to_rel(root, install_xtftestdir), test.name, "info.json")
        emit_build(
            lines,
            install_info,
            "install_data",
            [info_output],
            {"outdir": os.path.dirname(install_info)},
        )
        install_targets.append(install_info)

        for env_name in test.envs:
            env = envs[env_name]

            dep_outputs: list[str] = []
            for obj in objects_perbits:
                rel_obj = to_rel(root, obj)
                output_obj = rel_obj[:-2] + f"-{env.arch}.o"
                emit_object(obj, output_obj, env.cflags_arch if source_for_obj(root, obj)[1] == "cc" else env.aflags_arch)
                dep_outputs.append(output_obj)

            for obj in env_objects.get(env_name, []) + objects_perenv + test.local_objs:
                rel_obj = to_rel(root, obj)
                output_obj = rel_obj[:-2] + f"-{env_name}.o"
                emit_object(obj, output_obj, env.cflags_env if source_for_obj(root, obj)[1] == "cc" else env.aflags_env)
                dep_outputs.append(output_obj)

            link_script = emit_link_script(env_name)
            bin_output = os.path.join(test.directory, f"test-{env_name}-{test.name}")
            link_inputs = dep_outputs
            link_vars = {"ldflags": env.ldflags}
            link_rule = "link"
            if env_name == "hvm64":
                link_rule = "link_hvm64"
                link_vars["tmpout"] = bin_output + ".tmp"
            emit_build(lines, bin_output, link_rule, link_inputs, link_vars, implicit_inputs=[link_script])
            build_targets.append(bin_output)

            install_bin = os.path.join(to_rel(root, install_xtftestdir), test.name, os.path.basename(bin_output))
            emit_build(
                lines,
                install_bin,
                "install_program",
                [bin_output],
                {"outdir": os.path.dirname(install_bin)},
            )
            install_targets.append(install_bin)

            cfg_output = os.path.join(test.directory, f"test-{env_name}-{test.name}.cfg")
            cfg_inputs = ["build/mkcfg.py", to_rel(root, env.defcfg), os.path.join(test.directory, "Makefile")]
            if test.extra_cfg:
                cfg_inputs.append(to_rel(root, test.extra_cfg))
            emit_build(
                lines,
                cfg_output,
                "mkcfg",
                cfg_inputs,
                {
                    "defcfg": to_rel(root, env.defcfg),
                    "vcpus": test.vcpus,
                    "extracfg": to_rel(root, test.extra_cfg),
                    "varycfg": "",
                },
            )
            build_targets.append(cfg_output)

            install_cfg = os.path.join(to_rel(root, install_xtftestdir), test.name, os.path.basename(cfg_output))
            emit_build(
                lines,
                install_cfg,
                "install_data",
                [cfg_output],
                {"outdir": os.path.dirname(install_cfg)},
            )
            install_targets.append(install_cfg)

            for variation in test.vary_cfg:
                local_vary = os.path.join(test.directory, f"{variation}.cfg.in")
                vary_input = local_vary if os.path.exists(os.path.join(root, local_vary)) else os.path.join("config", f"{variation}.cfg.in")
                vary_output = os.path.join(test.directory, f"test-{env_name}-{test.name}~{variation}.cfg")
                vary_inputs = ["build/mkcfg.py", to_rel(root, env.defcfg), os.path.join(test.directory, "Makefile"), vary_input]
                if test.extra_cfg:
                    vary_inputs.append(to_rel(root, test.extra_cfg))
                emit_build(
                    lines,
                    vary_output,
                    "mkcfg",
                    vary_inputs,
                    {
                        "defcfg": to_rel(root, env.defcfg),
                        "vcpus": test.vcpus,
                        "extracfg": to_rel(root, test.extra_cfg),
                        "varycfg": vary_input,
                    },
                )
                build_targets.append(vary_output)

                install_vary = os.path.join(to_rel(root, install_xtftestdir), test.name, os.path.basename(vary_output))
                emit_build(
                    lines,
                    install_vary,
                    "install_data",
                    [vary_output],
                    {"outdir": os.path.dirname(install_vary)},
                )
                install_targets.append(install_vary)

    runner_install = os.path.join(to_rel(root, install_xtfdir), "xtf-runner")
    emit_build(
        lines,
        runner_install,
        "install_program",
        ["xtf-runner"],
        {"outdir": os.path.dirname(runner_install)},
    )
    install_targets.append(runner_install)

    emit_phony(lines, "build", build_targets)
    emit_phony(lines, "install", install_targets)
    lines.append("default build")
    lines.append("")
    return lines


def main() -> int:
    if len(sys.argv) != 3:
        print("Usage: gen-ninja.py MANIFEST OUT", file=sys.stderr)
        return 2

    manifest_path, output_path = sys.argv[1:3]
    globals_map, envs, env_objects, tests, objects_perbits, objects_perenv = parse_manifest(manifest_path)
    root = globals_map["ROOT"]
    lines = build_ninja(root, globals_map, envs, env_objects, tests, objects_perbits, objects_perenv)

    with open(output_path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines))

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
