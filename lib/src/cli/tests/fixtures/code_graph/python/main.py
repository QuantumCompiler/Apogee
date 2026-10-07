import json

from pkg import service


def main():
    print(json.dumps(service.run("/tmp/x")))


main()
