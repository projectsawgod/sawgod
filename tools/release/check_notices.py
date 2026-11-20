"""ROM-free check of every platform's required licenses, README and notice references."""
import notices


def main():
    for target in notices.PLATFORM:
        text = notices.notice_text(target)
        readme = notices.ROOT / f'tools/release/README-{target}.txt'
        if not readme.is_file() or not readme.stat().st_size:
            raise SystemExit(f'Missing player README: {target}')
        for component, _, _, files, _ in notices.COMMON + notices.PLATFORM[target]:
            for name in files:
                path = notices.ROOT / 'licenses' / name
                if not path.is_file() or not path.stat().st_size:
                    raise SystemExit(f'{target}: missing license for {component}: {name}')
                if 'licenses/' + name not in text:
                    raise SystemExit(f'{target}: license omitted from notice: {name}')
        print(f'{target}: license inventory and README present')
        if target != 'android':
            if not (notices.ROOT / 'tools/release/README-decomp.txt').is_file():
                raise SystemExit('Missing decomp player README')
            text = notices.decomp_notice_text(target)
            for component, _, _, files, _ in notices.decomp_entries(target):
                for name in files:
                    if not (notices.ROOT / 'licenses' / name).is_file() or 'licenses/' + name not in text:
                        raise SystemExit(f'{target} decomp: missing license for {component}: {name}')
            print(f'{target} decomp: license inventory and README present')


if __name__ == '__main__':
    main()
