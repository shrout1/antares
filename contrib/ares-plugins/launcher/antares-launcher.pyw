# Runs antares-launcher on Windows, where a file without an extension can't be opened with
# Python by double-clicking it. pythonw.exe opens .pyw files without a console window.
import os
import runpy

runpy.run_path(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'antares-launcher'),
               run_name='__main__')
