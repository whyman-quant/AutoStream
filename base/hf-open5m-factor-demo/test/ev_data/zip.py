# -*- coding: utf-8 -*-
"""
# 1) 把路径下的内容打包成 zip，默认「不包括」最外层目录名（zip 根下直接是原目录里的文件/子目录）
python3.8 zip.py zip ./my_project ./my_project.zip

# 1b) 需要 zip 内保留最外层目录名（解压后出现 my_project/ 或 20251224/ 这一层）时加 --keep-root
python3.8 zip.py zip ./local/factor_ev/20251224 ./live/ev_output_20251224_666666_day.csv --keep-root

# 2) 把 zip 解压到指定目录
python3.8 zip.py unzip ./my_project.zip ./extract_here
"""

import os
import sys
import zipfile


def zipDir(dirpath, outFullName, keep_root=False):
    """
    将目录 dirpath 打包为 outFullName（.zip 或任意路径，由调用方决定扩展名）
    参数:
        dirpath    : 待压缩目录
        outFullName: 生成的 zip 文件完整路径
        keep_root  : False（默认）时 zip 内路径相对 dirpath，不包含 dirpath 最后一级目录名；
                    True 时相对 dirpath 的父目录打包，zip 内会包含 os.path.basename(dirpath)/ 这一层。
    """
    dirpath = os.path.abspath(os.path.normpath(dirpath))
    # 相对何者计算 zip 内路径：默认相对被压缩目录本身（不包含其文件夹名）；keep_root 时相对其父目录
    arc_base = os.path.dirname(dirpath) if keep_root else dirpath

    # 创建 ZipFile 对象，ZIP_DEFLATED 压缩率更高；ZIP_STORED 无压缩
    with zipfile.ZipFile(outFullName, "w", zipfile.ZIP_DEFLATED) as zf:
        # 遍历目录
        for root, dirs, files in os.walk(dirpath):
            # 计算相对路径，保证 zip 内部目录结构
            arc_root = os.path.relpath(root, arc_base)
            for f in files:
                src_path = os.path.join(root, f)
                arc_path = os.path.join(arc_root, f) if arc_root != '.' else f
                zf.write(src_path, arc_path)
            # 空目录也要写进去，否则解压后丢失
            if not files and not dirs:
                keep_arc = os.path.join(arc_root, '.keep') if arc_root != '.' else '.keep'
                zf.writestr(keep_arc, '')

def unzipDir(zippath, targetdir):
    """
    将 zippath 解压到 targetdir
    参数:
        zippath  : zip 文件路径
        targetdir: 解压目标目录
    """
    os.makedirs(targetdir, exist_ok=True)
    with zipfile.ZipFile(zippath) as zf:
        zf.extractall(targetdir)

def parse_argv(args):
    """
    解析命令行参数
    返回 (cmd, source, target, keep_root)；keep_root 仅对 zip 有效
    """
    if len(args) < 4 or len(args) > 5:
        print("用法: python3.8 zip.py <zip|unzip> <source> <target> [--keep-root]")
        print("  zip  时可选 --keep-root：zip 内保留被压缩目录的最后一级文件夹名")
        sys.exit(1)
    cmd = args[1]
    source = os.path.expanduser(args[2])
    target = os.path.expanduser(args[3])
    keep_root = False
    if len(args) == 5:
        if args[4] not in ('--keep-root', '-r'):
            print("❌ 未知选项，zip 仅支持可选参数: --keep-root 或 -r")
            sys.exit(1)
        keep_root = True
    return cmd, source, target, keep_root

if __name__ == '__main__':
    cmd, source, target, keep_root = parse_argv(sys.argv)
    try:
        if cmd == "zip":
            zipDir(source, target, keep_root=keep_root)
            print(f"✅ 压缩完成 -> {os.path.abspath(target)}")
        elif cmd == "unzip":
            unzipDir(source, target)
            print(f"✅ 解压完成 -> {os.path.abspath(target)}")
        else:
            print("❌ 只支持 zip / unzip 两种命令")
    except Exception as e:
        print(f"❌ 操作失败: {e}")