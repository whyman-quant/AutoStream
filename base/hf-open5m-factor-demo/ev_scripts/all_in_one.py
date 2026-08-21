import os
import sys
import abc
import json
import shutil
from typing import Any, Dict

import h5py
import numpy as np
import pandas as pd

from my.data import basic_func, meta_api, quote


class EVGenerator(abc.ABC):
    """Base class for EV generators."""

    def __init__(self):
        self.name = type(self).__name__.lower().replace('evgenerator', '')

    @abc.abstractmethod
    def generate(self, cfg: Dict[str, Any]) -> None:
        """Generate EV data.

        Args:
            cfg: Configuration dictionary containing parameters.
        """
        pass


class ChensiEVGenerator(EVGenerator):
    """Chensi EV generator implementation."""

    def __init__(self):
        super().__init__()
        self.fields = [
            'high',
            'low',
            'open',
            'close',
            'vwap',
            'vol',
            'amount',
            'adj',
            'ashare',
        ]

    def generate(self, cfg: Dict[str, Any]) -> None:
        """Generate Chensi EV data.

        Args:
            cfg: Configuration dictionary containing date and output path.
        """
        output_dir = os.path.join(cfg.pop('output'), self.name)
        os.makedirs(output_dir, exist_ok=True)

        self._generate_per1day(
            {**cfg, 'output': os.path.join(output_dir, 'per1day.csv')}
        )

    def _generate_per1day(self, cfg: Dict[str, Any]) -> None:
        """Generate Chensi EV data.

        Args:
            cfg: Configuration dictionary containing date and output path.
        """
        date = cfg['date']
        date_str = date.replace('.', '')

        df = quote.factor(int(date_str), 280, 233114, 0, 0, pandas=True)
        df['ticker'] = df['ticker'].str.decode(encoding='utf8')
        df = df[df['ticker'].str[0].isin(['0', '3', '6'])]
        df = df[['ticker'] + self.fields]

        df['vol'] *= df['adj']
        for c in ['high', 'low', 'open', 'close', 'vwap']:
            df[c] /= df['adj']
            df[c] *= 10000
        df.drop(columns=['adj'], inplace=True)

        # Write result to file
        df.to_csv(cfg['output'], index=False)


class TanganEVGenerator(EVGenerator):
    """Tangan EV generator implementation."""

    def __init__(self):
        super().__init__()

    def generate(self, cfg: Dict[str, Any]) -> None:
        """Generate Tangan EV data.

        Args:
            cfg: Configuration dictionary containing date and output path.
        """
        output_dir = os.path.join(cfg.pop('output'), self.name)
        os.makedirs(output_dir, exist_ok=True)

        self._generate_day_for_open(
            {**cfg, 'output': os.path.join(output_dir, 'day_for_open.h5')}
        )

    def _generate_day_for_open(self, cfg: Dict[str, Any]) -> None:
        """Generate Tangan EV data.

        Args:
            cfg: Configuration dictionary containing date and output path.
        """
        date = cfg['date']
        output = cfg['output']

        h5_store = h5py.File(output, 'w')
        dt = date.replace('.', '')
        dt = self._get_last_tradedate(dt, 1)
        df = basic_func.get_basic_data(int(dt), 'ashare_eod')
        df['S_DQ_VOLUME'] = df['S_DQ_VOLUME'] * 100
        df['S_DQ_AMOUNT'] = df['S_DQ_AMOUNT'] * 1000

        data = self._df2np(df, 'S_DQ_CLOSE')
        h5_store.create_dataset('pre_close', data=data, shape=data.shape)
        data = self._df2np(df, 'S_DQ_VOLUME')
        h5_store.create_dataset('pre_volume', data=data, shape=data.shape)
        data = self._df2np(df, 'S_DQ_AMOUNT')
        h5_store.create_dataset('pre_amount', data=data, shape=data.shape)
        h5_store.close()

    @staticmethod
    def _df2np(df, name):
        """Convert DataFrame column to structured numpy array.

        Args:
            df: DataFrame containing SYMBOL and data columns.
            name: Name of the data column to convert.

        Returns:
            Structured numpy array with SYMBOL and DATA fields.
        """
        symbol = df['SYMBOL'].values.astype('S9')
        data = df[name].values.astype('f8')
        n = len(symbol)
        values = []
        for i in range(n):
            values.append((symbol[i], data[i]))
        dtype = np.dtype([('SYMBOL', 'S9'), ('DATA', 'f8')], align=True)
        return np.array(values, dtype=dtype)

    @staticmethod
    def _get_last_tradedate(dt, n):
        """Get the last n trade date before dt.

        Args:
            dt: Date string in format YYYYMMDD.
            n: Number of days to look back.

        Returns:
            Last trade date in format YYYYMMDD.
        """
        y, m, d = dt[0:4], dt[4:6], dt[6:8]
        dt_fmt = f'{y}-{m}-{d}'
        trade_list = basic_func.npq_calendar(b'SSE', '1990-01-01', dt_fmt)[
            'TRADE_DT'
        ].tolist()
        return trade_list[-(n + 1)].strftime('%Y%m%d')


class MengmaiEVGenerator(EVGenerator):
    """Mengmai EV generator implementation."""

    def __init__(self):
        super().__init__()

    def generate(self, cfg: Dict[str, Any]) -> None:
        """Generate Mengmai EV data.

        Args:
            cfg: Configuration dictionary containing date and output path.
        """
        output_dir = os.path.join(cfg.pop('output'), self.name)
        os.makedirs(output_dir, exist_ok=True)

        self._generate_codelist(
            {
                **cfg,
                'output': os.path.join(output_dir, 'lab200007_codelist.h5'),
            }
        )

    def _generate_codelist(self, cfg: Dict[str, Any]) -> None:
        """Generate Mengmai EV data.

        Args:
            cfg: Configuration dictionary containing date and output path.
        """
        date = cfg['date'].replace('.', '')
        alltradedays = [
            d.replace('-', '')
            for d in meta_api.get_trading_date_range(
                20150101, int(date), 'SSE'
            )
        ]
        dir_mxwork = cfg.get(
            'dir_mxwork',
            '/mnt/beegfs_dev/storage_r/706_wgh/app_working_dir/data/AlphaComm/MXer01',
        )

        tickers, ticker_index = self.get_codes(dir_mxwork, date)
        tickers_num = len(tickers)

        pre1date_st = self.get_basedata(
            'st',
            'per1day',
            date,
            np.arange(-1, 0),
            dir_mxwork,
            alltradedays,
            tickers_num,
        )
        pre1date_ipodates = self.get_basedata(
            'ipodates',
            'per1day',
            date,
            np.arange(-1, 0),
            dir_mxwork,
            alltradedays,
            tickers_num,
        )
        pre1date_swhy = self.get_basedata(
            'swhy',
            'per1day',
            date,
            np.arange(-1, 0),
            dir_mxwork,
            alltradedays,
            tickers_num,
        )
        pre1date_ashare = self.get_basedata(
            'ashare',
            'per1day',
            date,
            np.arange(-1, 0),
            dir_mxwork,
            alltradedays,
            tickers_num,
        )
        pre1date_vwap = self.get_basedata(
            'vwap',
            'per1day',
            date,
            np.arange(-1, 0),
            dir_mxwork,
            alltradedays,
            tickers_num,
        )

        codelist01 = []
        codelist01_idx = []
        codelist01_swhy = []
        for ticker, ticker_i in ticker_index.items():
            condition_list = [
                pre1date_st[ticker_i, -1] == 0,
                pre1date_ipodates[ticker_i, -1] > 20,
                len(str(pre1date_swhy[ticker_i, -1]['data'])) > 5,
            ]
            if all(condition_list):
                codelist01.append(ticker)
                codelist01_idx.append(ticker_i)
                codelist01_swhy.append(pre1date_swhy[ticker_i, -1]['data'])
        codelist01_idx = np.array(codelist01_idx)
        # 最开始运行的一天会是这样的
        if len(codelist01) == 0:
            return

        # 计算最新的流通市值
        mv = (pre1date_vwap[:, -1] * pre1date_ashare[:, -1])[codelist01_idx]
        xmat = np.zeros(
            (len(codelist01), len(set(codelist01_swhy)) + 1), np.float32
        )
        xmat[:, : len(set(codelist01_swhy))] = pd.get_dummies(
            codelist01_swhy
        ).values
        xmat[:, -1] = np.log(mv)
        xmat[:, -1] = self.deleteoutliers(xmat[:, -1], 0.01, 3, True, True)

        save_file_tmp = f'{cfg["output"]}.tmp'
        save_file = cfg['output']
        os.makedirs(os.path.dirname(save_file), exist_ok=True)
        with h5py.File(save_file_tmp, 'w') as h:
            dset2 = h.create_dataset(
                name='codelist',
                shape=(len(codelist01),),
                dtype=np.dtype([('code', 'S12')]),
            )
            for i in range(len(codelist01)):
                dset2[i] = (codelist01[i],)
            h.create_dataset(
                name='codelist_idx',
                shape=codelist01_idx.shape,
                dtype=int,
                data=codelist01_idx,
            )
            h.create_dataset(
                name='xmat', shape=xmat.shape, dtype=np.float32, data=xmat
            )
        os.rename(save_file_tmp, save_file)

    def get_codes(
        self,
        dir_mxwork: str,
        date: str,
        file_name='codes_all',
        decode=True,
        suffix=True,
    ):
        """读取股票代码

        Parameters
        ----------
        dir_mxwork : str
            工作路径
        date : str
            日期，格式为%Y%m%d
        file_name : str
            文件名，by default codes_all
        decode : bool
            是否解码，by default True
        suffix : bool
            是否包含后缀，by default False

        Returns
        -------
        list
            股票代码列表
        dict
            {股票代码：索引}对
        """
        codefile = os.path.join(
            dir_mxwork, 'basedata', date, 'per1day', '{}.h5'.format(file_name)
        )
        with h5py.File(codefile, 'r') as codeh:
            codelist = codeh['data'][()]['code']
        if decode:
            if suffix:
                tickers = [c.decode() for c in codelist]
            else:
                tickers = [c.decode()[:6] for c in codelist]
        else:
            if suffix:
                tickers = [c for c in codelist]
            else:
                tickers = [c[:6] for c in codelist]
        ticker_index = dict(zip(tickers, np.arange(len(tickers))))

        return tickers, ticker_index

    def get_basedata(
        self,
        data_name: str,
        data_type: str,
        date: str,
        date_offset,
        dir_mxwork: str,
        alltradedays: list,
        tickers_num=None,
    ):
        """获取或重复使用的基础数据

        Parameters
        ----------
        data_name : str
            数据名，可以为{strategy}__{feature}形式
        data_type : str
            数据类型,可选per1day, zyyx, wind, hibor
        date : str
            日期，格式为%Y%m%d
        date_offset : array_like
            回溯天数索引
        dir_mxwork : str
            工作路径
        alltradedays : list
            所有交易日
        tickers_num : int, optional
            股票数量, by default None

        Returns
        -------
        numpy array
            保存数据的数据，维度为[len(tickers_num), len(date_offset)]
        """
        if tickers_num is None:
            tickers, ticker_index = self.get_codes(dir_mxwork, date)
            tickers_num = len(tickers)

        if len(data_name.split('__')) > 1:
            data_name = os.path.join(*(data_name.split('__')))
        date_index = alltradedays.index(date)
        # 预设置数据维度
        shape = (tickers_num, len(date_offset))
        if data_name == 'swhy':
            data = np.full(
                shape, fill_value=(b'nan',), dtype=np.dtype([('data', 'S500')])
            )
        else:
            data = np.full(shape, fill_value=np.nan, dtype=np.float64)
        for index, offset in enumerate(date_offset):
            date_offset_i = alltradedays[date_index + offset]
            data_file = os.path.join(
                dir_mxwork,
                'basedata',
                date_offset_i,
                data_type,
                '{}.h5'.format(data_name),
            )
            with h5py.File(data_file, 'r') as f:
                tmp = f['data'][:]
            if tmp.ndim == 2:
                data[: tmp.shape[0], index] = tmp[:tickers_num, 0]
            else:
                data[: tmp.shape[0], index] = tmp[:tickers_num]

        data = data.reshape((tickers_num, -1))
        return data

    def deleteoutliers(self, x, cutr=0.01, stdr=5, tozero=False, toone=False):
        """截断离群值

        Parameters
        ----------
        x : array_like
            Array containing elements to clip.
        cutr : float, optional
            排序后前后截断的比例, by default 0.01
        stdr : int, optional
            截断此倍数的标准差, by default 5
        tozero : bool, optional
            是否去均值, by default False
        toone : bool, optional
            是否除标准差, by default False

        Returns
        -------
        array_like
            截断后的数据
        """
        n = int(len(x) * cutr)
        xsort = np.sort(x)
        minvalue = xsort[n]
        maxvalue = xsort[-n - 1]
        if minvalue == maxvalue:
            return x
        x1 = np.clip(x, minvalue, maxvalue)
        mean = np.nanmean(x1)
        std = np.nanstd(x1)
        minvalue = mean - stdr * std
        maxvalue = mean + stdr * std
        x2 = np.clip(x1, minvalue, maxvalue)
        if tozero:
            x2 = x2 - np.nanmean(x2)
        if toone:
            x2std = np.nanstd(x2)
            x2std = 1 if x2std == 0 else x2std
            x2 = x2 / x2std
        return x2


class EVManagerFactory:
    """Factory class for creating EV generators."""

    @staticmethod
    def create_generators() -> Dict[str, EVGenerator]:
        """Create all registered EV generators.

        Returns:
            Dictionary mapping generator names to generator instances.
        """
        generators = {}
        generator_classes = [
            ChensiEVGenerator,
            TanganEVGenerator,
            MengmaiEVGenerator,
        ]
        for cls in generator_classes:
            instance = cls()
            generators[instance.name] = instance
        return generators


class FileUtils:
    """Utility functions for file operations."""

    @staticmethod
    def compress_directory(
        source_dir: str, output_file: str, format: str = 'zip'
    ) -> str:
        """Compress a directory into an archive file.

        Args:
            source_dir: Source directory path.
            output_file: Output file path (without extension).
            format: Compression format ('zip', 'tar', 'gztar', 'bztar', 'xztar').

        Returns:
            Path to the created archive file.

        Raises:
            FileNotFoundError: If source directory doesn't exist.
        """
        if not os.path.exists(source_dir):
            raise FileNotFoundError(
                f'Source directory not found: {source_dir}'
            )

        output_dir = os.path.dirname(output_file)
        if output_dir and not os.path.exists(output_dir):
            os.makedirs(output_dir)

        try:
            archive_path = shutil.make_archive(
                base_name=output_file,
                format=format,
                root_dir=os.path.dirname(source_dir),
                base_dir=os.path.basename(source_dir),
            )
            print(f'Compression complete! File saved to: {archive_path}')
            return archive_path
        except Exception as e:
            print(f'Error during compression: {e}')
            raise


class StrategyConfigGenerator:
    """Class for generating strategy configurations."""

    def __init__(self):
        self.generators = EVManagerFactory.create_generators()

    def generate(self, cfg: Dict[str, Any], compress: bool = True) -> None:
        """Generate strategy configuration.

        Args:
            cfg: Configuration dictionary containing date and output path.
        """
        date = cfg['date']
        output = cfg['output']
        date_str = date.replace('.', '')

        # Create output directories
        output_dir = os.path.join(os.path.dirname(output), date_str)
        os.makedirs(output_dir, exist_ok=True)

        # Generate data for each generator
        for generator in self.generators.values():
            generator.generate(
                {
                    'date': date,
                    'output': output_dir,
                }
            )

        if compress:
            FileUtils.compress_directory(output_dir, output)
            # Remove zip file's extension
            shutil.move(output + '.zip', output)


def generate_strategy_config(cfg, compress: bool = True):
    """Generate strategy configuration.

    Args:
        cfg: Configuration dictionary containing date and output path.
    """
    StrategyConfigGenerator().generate(cfg, compress)


if __name__ == '__main__':
    if len(sys.argv) == 1:
        date = '2022.02.22'
        # Command-line argument for testing
        cfg = f'{{"date": "{date}", "output": "./output/open_ev_files_{date.replace(".", "")}.csv"}}'
    else:
        # Command-line argument from the system
        cfg = sys.argv[1]

    generate_strategy_config(json.loads(cfg), compress=True)
