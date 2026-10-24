# The Pi: one layout for every project on it

Several projects run on the TV's Raspberry Pi 1, a 4 GB SD card and 426 MB of RAM. So that each
of them stays easy to find, update and take off again, they all follow the layout below. tvtv is
the base: its setup (`tools/setup-remote.sh`) makes the service user, the keys, sshd, sudo, the
USB stick and the limits, and installs `tvtv-pi`, which keeps the rest in order. Every other
project is a guest: it puts its files where this page says, and it writes a list of them.

| Component | Repo | What | Needs |
|---|---|---|---|
| `show` | tvtv | the show on tty1, the remote (`ssh zapper`), Doom, the base | – |
| `social` | dem02socialTV | tvtv-social, Mastodon to the social channels 24 to 26 | `show` |

The AV input (streams, clips) and the native Doom engine on tvtv's branches `feat/av` and
`feat/doom` take the names `av` and `doom` when they land, with units `tvtv-av.*` and `tvtv-doom.*`.

## Layout

| Where | What | Owner |
|---|---|---|
| `/opt/tvtv/` | the show's tree: sources, build, uploaded `visuals/`, Doom's saves `doom/`, `VERSION` | `zapper` (it builds uploads) |
| `/usr/local/lib/tvtv/` | every component's programs, named after it (`tv-dispatch`, `tvtv-social`) | root |
| `/usr/local/sbin/tvtv-*`, `tv-keys` | commands for a person with sudo | root |
| `/etc/tvtv/` | settings: `authorized_keys`, `<component>.json`, secrets `<component>.token` (0600) | root |
| `/etc/systemd/system/tvtv-<component>.*` | the units; drop-ins (`systemctl edit`) are the site's settings | root |
| `/etc/sudoers.d/tvtv-<component>` | the deploying login's exact rules for that component | root |
| `/etc/**.d/50-tvtv*` | drop-ins into the system's own settings (sshd, journald, apt) | root |
| `/var/lib/tvtv-<component>/` | state, made by systemd (`StateDirectory=`) | the service |
| `/var/lib/tvtv/components/<component>` | what the component installed, for `tvtv-pi` | root |
| `/run/tvtv-<component>.sock` | its control socket, made by `tvtv-<component>.socket` | root, group `zapper` |
| `/run/tvtv-<component>/` | its runtime files (`RuntimeDirectory=`): anim.log, the snapshot | the service |
| `/srv/tvtv/` | the USB stick: `doom/` (WADs), `clips/` | see below |
| `~/tvtv-deploy/<what>/` | copies a deploy syncs to the deploying login before root installs them | that login |

Two rules carry the security of all of it, as before: a file the service user can write never
reaches root (root installs from a copy it owns, read whole first), and whatever parses input from
outside runs in a sandbox of its own (`DynamicUser=`, no devices, read-only system).

The socket sits next to `/run/tvtv-<component>/`, not in it: a socket unit's file must outlive the
service, and systemd removes a `DynamicUser=` service's runtime directory when it stops.

## Names

Everything that runs or is installed is called `tvtv-<component>`. The show used to be
`bc101.service`; `bc101` stays the name of the program (the bitcircus101 demo show, `/opt/tvtv/bc101`),
and its own settings keep their names (`BC101_SOCIAL`, `BC101_DEM0_BUDGET`, `BC101_DEM0_MS`,
`BC101_CTL`).

| Before | Now |
|---|---|
| `bc101.service`, `bc101.socket` | `tvtv-show.service`, `tvtv-show.socket` |
| `/run/bc101/ctl` | `/run/tvtv-show.sock` |
| `/run/bc101/anim.log` | `/run/tvtv-show/anim.log` |
| `/etc/sudoers.d/tvtv-deploy` | `/etc/sudoers.d/tvtv-show` |
| the stick: vfat, `/dev/sda1` on `/mnt` | ext4 with the label `TVTV` on `/srv/tvtv` |
| `~/tvtv-setup`, `~/tvtv-social-setup`, `~/tvtv-social-check` | `~/tvtv-deploy/show-setup`, `social`, `social-check` |
| `.env`: `BC101_HOST`, `BC101_SETUP_HOST`, `BC101_DIR`, `BC101_USER` | `TVTV_DEPLOY_HOST`, `TVTV_SETUP_HOST`, `TVTV_HOME`, `TVTV_USER` |

The old `.env` keys still work and print a note; rename them when it suits. tvtv and
dem02socialTV read the same `.env` keys (their loaders are the same file), so one `.env` with the
Pi's logins serves both.

## Component lists

Each installer writes `/var/lib/tvtv/components/<component>`, root's, one entry per line:

```
# tvtv-social, from dem02socialTV; written by deploy/install.sh
name social
repo https://github.com/bmmmm/dem02socialTV
version 3f2c1a9e0b7d
needs show
unit tvtv-social.socket
unit tvtv-social.service
file /usr/local/lib/tvtv/tvtv-social
keep /etc/tvtv/social.json
keep /etc/tvtv/social.token
keep /var/lib/tvtv-social
```

| Key | Meaning |
|---|---|
| `name` | the component, the same as the file's name |
| `repo` | where it comes from |
| `version` or `version-file` | what is installed, or a file whose first line says so |
| `needs` | a component that must be there (the show, for the group and the dispatcher) |
| `unit` | a unit in `/etc/systemd/system`, named `tvtv-*` |
| `file` | installed by it, goes with `tvtv-pi remove` |
| `keep` | settings, secrets, state: they stay on `remove` and go with `--purge` |

`tvtv-pi` refuses a list that names a path outside the layout above, so a list cannot make it
remove anything else. A new project on the Pi takes these steps:

1. Pick a component name; install units `tvtv-<name>.*`, programs into `/usr/local/lib/tvtv/`,
   settings into `/etc/tvtv/<name>.*`, state with `StateDirectory=tvtv-<name>`.
2. Sync to `~/tvtv-deploy/<name>/` and install as root from there, never from a tree a service
   user owns.
3. Write the list last, after everything in it is in place.
4. `sudo tvtv-pi check` must say ok.

## tvtv-pi

```
tvtv-pi status            the components, their versions, units and files
tvtv-pi du                where the space goes: card, stick, components, apt, journal, swap, copies
tvtv-pi check             this layout: lists sound, nothing unclaimed, nothing left from before
sudo tvtv-pi clean [-n]   apt's cache, old journal files, copies left behind (-n: show only)
sudo tvtv-pi remove <component> [--purge]
```

The deploying login may run `status`, `du` and `check` with sudo without a password:
`ssh tvtv-deploy sudo tvtv-pi du`. `check` names whatever sits in the shared places
(`/usr/local/lib/tvtv`, `/etc/tvtv`, the units, sudoers) without a list that claims it.

## The USB stick

One stick for everything that is too big for the card: Doom's WADs in `doom/` (the deploying
login copies them there), clips in `clips/` (the service user writes them). It is ext4 with the
label `TVTV`, mounts on `/srv/tvtv` the first time something looks there and unmounts after a
minute idle, so it can be pulled; without it the Pi boots as usual, and a look at `/srv/tvtv` waits
up to 2 s. To make one, on the Pi (this erases the stick; copy what is on it first):

```
lsblk -f                                   # which one is the stick: sda, usually
sudo mkfs.ext4 -L TVTV /dev/sda1
sudo tools/setup-remote.sh ...             # or scripts/deploy.sh --setup: makes doom/ and clips/
cp *.wad /srv/tvtv/doom/                   # as the deploying login
```

The setup leaves an unlabelled or vfat stick alone and says so. The old vfat stick on `/mnt`
mounts no more once the setup has run again: put its WADs on the new one.

## Room on the SD card

The card was 94 % full on 2026-10-18 (195 MB free). The setup keeps it from filling up again:
the journal takes at most 32 MB on the card and 8 MB of RAM (`/etc/systemd/journald.conf.d/50-tvtv.conf`),
and apt keeps no `.deb` files after an install (`/etc/apt/apt.conf.d/50-tvtv`).
`sudo tvtv-pi du` shows the rest, with the twelve largest packages at the end. Packages are never
removed by a script: gcc and make build the uploads, the GStreamer and Mesa packages are the AV
input's, NetworkManager and wpa_supplicant keep the Pi on the network. The swap file `/var/swap`
(426 MB, rpi-swap) is worth its room while apt runs with 426 MB of RAM; shrink it only with the
show stopped and never during an apt run.

## Moving over from before

On a Pi set up before this page, in this order:

1. `scripts/deploy.sh --setup` in tvtv (with the new tree). The setup stops and removes
   `bc101.service`/`.socket`, the old sudo rules and the stick's `/mnt` line, installs
   `tvtv-show.*`, `tvtv-pi` and the limits, writes the list `show` and starts the show again.
   A plain `scripts/deploy.sh` refuses until then: the old sudo rules know no `tvtv-show`.
   Drop-ins for `bc101.service` (`BC101_DEM0_BUDGET`, from `systemctl edit bc101`) move to
   `tvtv-show.service.d`.
2. `deploy/deploy.sh` in dem02socialTV, then its `install.sh`: it writes the list `social`.
3. A new stick, if wanted (above), then `scripts/deploy.sh --setup` once more for its folders.
4. `sudo tvtv-pi clean`, then `sudo tvtv-pi check`: ok.

## Removing

`sudo tvtv-pi remove social` stops a guest and takes its programs and units, and keeps its
settings, token and state for a later install; `--purge` takes those too. The show is the base and
is not removed that way. To clear the Pi of all of it, remove the guests first, then by hand:
`systemctl disable --now tvtv-show.socket tvtv-show.service srv-tvtv.automount`, the files in the
list `show`, the `# tvtv: the USB stick` lines in `/etc/fstab`, `/var/lib/tvtv`, and the user:
`userdel zapper`.
