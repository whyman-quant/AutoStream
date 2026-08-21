import os
import time
import random
from pathlib import Path
from my.data import meta_api
from joblib import Parallel, delayed
from datetime import datetime

# 日期范围配置
begT = 20220101
endT = 20251231
n_jobs = 20

# 路径配置
project_root = Path(__file__).resolve().parent
main_file = project_root / 'build/app_factor/main'
config_file = project_root / 'config.json'
slurm_path = project_root / 'slurm'

def create_cmd(date):
    thread_num = 10
    memory = 512  # 4GB，因为 open5m 数据量较大
    partition = random.choice(['cpu_wgh'])  # 'cpu_only', 'cpu_wgh'
    cmd = (
        f'mybatch -c {thread_num} -m {memory}G -p {partition} -t 8:00:00 -x 162 '
        f'-s "{main_file} date={date} thread_num={thread_num} config_file={config_file}" '
        '-e "LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/home/fangwei/wgh_team_share'
        f'/software/my_new_hdf5/lib" '
    )
    return cmd

def _os_system(cmd):
    import subprocess
    try:
        output = subprocess.check_output(cmd, shell=True, stderr=subprocess.STDOUT)
        time.sleep(5)
        print(output.decode().strip())
    except subprocess.CalledProcessError as e:
        print(f'Error: {e.output.decode()}')


if __name__ == '__main__':
    # 创建 slurm 任务目录
    this_task_path = os.path.join(slurm_path, f'{datetime.now().strftime("%Y%m%d_%H%M%S")}')
    if not os.path.exists(this_task_path):
        os.makedirs(this_task_path, exist_ok=True)
    os.chdir(this_task_path)

    # 获取交易日列表
    alldays = [d.replace('-', '') for d in meta_api.get_trading_date_range(begT, endT, 'SSE')]

    print(f'Total trading days: {len(alldays)}')
    print(f'Date range: {alldays[0]} - {alldays[-1]}')
    print(f'Slurm task path: {this_task_path}')

    # 并行提交任务
    p = Parallel(n_jobs=n_jobs)(delayed(lambda d: _os_system(create_cmd(d)))(d) for d in alldays)
