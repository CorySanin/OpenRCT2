# ![OpenRCT2 logo](https://github.com/CorySanin/OpenRCT2-FFA/raw/develop/resources/logo/icon_x128.png) OpenRCT2-FFA

OpenRCT2 is an [open-source re-implementation of RollerCoaster Tycoon 2](https://github.com/OpenRCT2/OpenRCT2). A construction and management simulation video game that simulates amusement park management.

This repository is a fork that focuses on adding functionality to the multiplayer aspect of OpenRCT2. In particular, by using this software for the server, players can only modify and demolish rides that they've created themselves.

This repository contains `Dockerfile` definitions for the command line version OpenRCT2. These images should be used for hosting multiplayer servers or executing commands such as generating screenshots.

You can pull and test the latest develop version by running:
```
$ docker run --rm corysanin/openrct2-ffa:develop --version
```

## Multiplayer

To host a multiplayer server, create a container that exposes the desired port and pass in a URL to the park to load. For example:

```
$ docker run --rm -p 11753:11753 -it corysanin/openrct2-ffa host https://bit.do/openrct2-bpb
```

All configuraion data is stored inside the container. If you want to persit it outside the container, you can mount it to a volume or your local filesystem. Mounting your local filesystem also allows you to read and write saved games locally. For example:

```
$ docker run --rm -p 11753:11753 -v /home/me/openrct2-config:/home/openrct2/.config/OpenRCT2 -it corysanin/openrct2-ffa host /home/openrct2/.config/OpenRCT2/save/mypark.sv6
```

The command above will mount the OpenRCT2 user / config directory inside the container to a directory on your local filesystem. This will allow you to persist and edit the configuration, saved games etc. locally.

It will then host a new server and load the saved game `mypark.sv6` located in the mounted directory under the save sub-directory.

## Tags

There are tags for the latest release snapshot as well as the latest* develop build. Each comes in [Alpine](https://github.com/CorySanin/OpenRCT2-FFA/blob/develop/dockerfiles/ffa/alpine/Dockerfile) and [Ubuntu](https://github.com/CorySanin/OpenRCT2-FFA/blob/develop/dockerfiles/ffa/ubuntu/Dockerfile) flavors. I recommend Alpine due to it's small size.

| Tag              | Description                    |
|------------------|--------------------------------|
| `develop-alpine` | Develop build on Alpine        |
| `develop-ubuntu` | Develop build on Ubuntu        |
| `release-alpine` | latest release build on Alpine |
| `release-ubuntu` | latest release build on Ubuntu |

\*The develop build gets updated periodically when changes are pulled from upstream. These changes tend to get pulled in faster when the network version changes.
