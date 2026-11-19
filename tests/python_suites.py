"""Load optional local test extensions without requiring them in source releases."""
from pathlib import Path
import runpy


def load_local_tests(namespace):
    source = Path(namespace['__file__'])
    companion = source.with_name('developer') / (source.stem.removeprefix('test_') + '_cases.py')
    if companion.parent.is_dir():
        additions = runpy.run_path(str(companion), init_globals=namespace,
                                   run_name=namespace['__name__'])
        namespace.update((key, value) for key, value in additions.items()
                         if not key.startswith('__'))
