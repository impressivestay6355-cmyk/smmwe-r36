#!/bin/bash

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

source "$controlfolder/control.txt"
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"
get_controls

SCRIPT_DIR=$(cd -- "$(dirname -- "$(readlink -f -- "$0" 2>/dev/null || echo "$0")")" 2>/dev/null && pwd -P)
if [ -n "$SCRIPT_DIR" ] && [ -f "$SCRIPT_DIR/smmwe/bin/gmloadernext.aarch64" ]; then
  GAMEDIR="$SCRIPT_DIR/smmwe"
else
  GAMEDIR="/$directory/ports/smmwe"
fi

cd "$GAMEDIR" || exit 1
> "$GAMEDIR/log.txt"
if echo | sed -u -e '' >/dev/null 2>&1; then
  exec > >(sed -u -e 's/InputResult( "[^"]*"/InputResult( "***"/' | tee "$GAMEDIR/log.txt") 2>&1
else
  exec > >(tee "$GAMEDIR/log.txt") 2>&1
fi
echo "[smmwe] gamedir=$GAMEDIR cfw=${CFW_NAME:-?} device=${DEVICE_NAME:-?}"

BIN="bin/gmloadernext.aarch64"
INSTALLER="$GAMEDIR/bin/smmwe-install"
SETUP="$GAMEDIR/Setup"
mkdir -p "$SETUP" "$GAMEDIR/data"
$ESUDO chmod +x "$GAMEDIR/$BIN" "$INSTALLER" 2>/dev/null

die() {
  local text
  text=$(printf '%b' "$*")
  echo "[smmwe] ERROR: $text"
  "$INSTALLER" "$GAMEDIR" --message "$text" 2>/dev/null
  command -v pm_finish >/dev/null 2>&1 && pm_finish
  exit 1
}

$ESUDO chmod a+r "$SETUP"/*.apk 2>/dev/null
"$INSTALLER" "$GAMEDIR"
case $? in
  0) echo "[smmwe] game data installed" ;;
  2) ;;
  *) echo "[smmwe] installation failed"
     command -v pm_finish >/dev/null 2>&1 && pm_finish
     exit 1 ;;
esac
[ -f "$GAMEDIR/data/smmwe.apk" ] ||
  die "No APK found.\nPut the SMM:WE APK (arm64) in ports/smmwe/Setup/"

mkdir -p "$GAMEDIR/data/storage" "$GAMEDIR/data/files" "$GAMEDIR/Import Levels/Mods and Textures" "$GAMEDIR/saves"

RESTORE=()
sysw() {
  local f="$1" v="$2" old
  [ -w "$f" ] || [ -n "$ESUDO" ] || return 1
  old=$(cat "$f" 2>/dev/null) || return 1
  [ "$old" = "$v" ] && return 0
  if echo "$v" | $ESUDO tee "$f" >/dev/null 2>&1; then
    RESTORE+=("$f|$old")
    return 0
  fi
  return 1
}
restore_sys() {
  local e
  for e in "${RESTORE[@]}"; do
    echo "${e#*|}" | $ESUDO tee "${e%%|*}" >/dev/null 2>&1
  done
}

device_tune() {
  local mem_kb swap_kb=0 zram_kb=0 zalgo="" line
  mem_kb=$(awk '/^MemTotal:/{print $2}' /proc/meminfo 2>/dev/null)
  while read -r line; do
    set -- $line
    case "$1" in Filename) continue ;; esac
    [ -n "$3" ] || continue
    swap_kb=$((swap_kb + $3))
    case "$1" in */zram*) zram_kb=$((zram_kb + $3)) ;; esac
  done < /proc/swaps 2>/dev/null
  if [ "$zram_kb" -gt 0 ]; then
    for z in /sys/block/zram*/comp_algorithm; do
      [ -r "$z" ] && zalgo=$(sed -n 's/.*\[\(.*\)\].*/\1/p' "$z") && break
    done
  fi
  echo "[smmwe] RAM $((mem_kb / 1024)) MB, swap $((swap_kb / 1024)) MB, zram $((zram_kb / 1024)) MB ${zalgo:+($zalgo)}"

  export MALLOC_ARENA_MAX=2
  if [ "$swap_kb" -ge 262144 ] || [ "${mem_kb:-0}" -ge 1800000 ]; then
    export MALLOC_TOP_PAD_=$((64 * 1024 * 1024))
    export MALLOC_TRIM_THRESHOLD_=$((128 * 1024 * 1024))
    export MALLOC_MMAP_THRESHOLD_=$((32 * 1024 * 1024))
  else
    export MALLOC_TOP_PAD_=$((16 * 1024 * 1024))
    export MALLOC_TRIM_THRESHOLD_=$((32 * 1024 * 1024))
    export MALLOC_MMAP_THRESHOLD_=$((8 * 1024 * 1024))
    [ "${mem_kb:-0}" -lt 900000 ] && echo "[smmwe] low RAM and no zram/swap: big levels may be slow to load"
  fi

  local p=/sys/devices/system/cpu/cpufreq/policy0
  [ -d "$p" ] || p=/sys/devices/system/cpu/cpu0/cpufreq
  if [ -r "$p/scaling_governor" ]; then
    local gov hwmax curmax avail top
    gov=$(cat "$p/scaling_governor")
    hwmax=$(cat "$p/cpuinfo_max_freq" 2>/dev/null)
    curmax=$(cat "$p/scaling_max_freq" 2>/dev/null)
    avail=$(cat "$p/scaling_available_frequencies" 2>/dev/null)
    top=$(echo "$avail $hwmax" | tr ' ' '\n' | grep -E '^[0-9]+$' | sort -n | tail -1)
    echo "[smmwe] CPU governor=$gov max=$((curmax / 1000)) MHz hw=$((top / 1000)) MHz"
    [ "${top:-0}" -gt 1296000 ] && echo "[smmwe] CPU overclock detected ($((top / 1000)) MHz)"
    if [ -n "$top" ] && [ "${curmax:-0}" -lt "$top" ]; then
      sysw "$p/scaling_max_freq" "$top" && echo "[smmwe] CPU cap lifted to $((top / 1000)) MHz while playing"
    fi
    if [ "$gov" = "ondemand" ]; then
      local od=/sys/devices/system/cpu/cpufreq/ondemand
      [ -d "$od" ] || od="$p/ondemand"
      sysw "$od/up_threshold" 60
      sysw "$od/sampling_down_factor" 4
    fi
  fi

  local g
  for g in /sys/class/devfreq/*; do
    [ -r "$g/governor" ] || continue
    case "$(basename "$g")$(cat "$g/name" 2>/dev/null)" in *gpu*|*mali*|*ff400000*) ;; *) continue ;; esac
    local ggov gmax gtop
    ggov=$(cat "$g/governor")
    gmax=$(cat "$g/max_freq" 2>/dev/null)
    gtop=$(tr ' ' '\n' < "$g/available_frequencies" 2>/dev/null | grep -E '^[0-9]+$' | sort -n | tail -1)
    echo "[smmwe] GPU $(basename "$g") governor=$ggov max=$((gmax / 1000000)) MHz hw=$((gtop / 1000000)) MHz"
    [ "${gtop:-0}" -gt 400000000 ] && echo "[smmwe] GPU overclock detected ($((gtop / 1000000)) MHz)"
    if [ -n "$gtop" ] && [ "${gmax:-0}" -lt "$gtop" ]; then
      sysw "$g/max_freq" "$gtop" && echo "[smmwe] GPU cap lifted to $((gtop / 1000000)) MHz while playing"
    fi
    break
  done
}
[ "${SMMWE_NO_TUNE:-0}" = "1" ] || device_tune

export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"
if [ "${CFW_NAME,,}" = "knulli" ]; then
  swap_abxy() { sed -e 's/,a:/,@A@:/g; s/,b:/,a:/g; s/,@A@:/,b:/g' \
                    -e 's/,x:/,@X@:/g; s/,y:/,x:/g; s/,@X@:/,y:/g'; }
  export SDL_GAMECONTROLLERCONFIG="$(printf '%s\n' "$SDL_GAMECONTROLLERCONFIG" | swap_abxy)"
  if [ -f "${SDL_GAMECONTROLLERCONFIG_FILE:-}" ]; then
    swap_abxy < "$SDL_GAMECONTROLLERCONFIG_FILE" > "$GAMEDIR/data/gamecontrollerdb_swapped.txt" &&
      export SDL_GAMECONTROLLERCONFIG_FILE="$GAMEDIR/data/gamecontrollerdb_swapped.txt"
  fi
  echo "[smmwe] knulli: A/B and X/Y swapped"
fi
export PORT_KEEP_ASPECT="${PORT_KEEP_ASPECT:-0}"
export PORT_STORAGE="$GAMEDIR/data/storage"
export PORT_FILES="$GAMEDIR/data/files"
export PORT_IMPORT="$GAMEDIR/Import Levels"
export PORT_FONT="$GAMEDIR/resources/fonts/NotoSans-Regular.ttf"
export PORT_CA_FILE="$GAMEDIR/resources/cacert.pem"
export PORT_KEYBOARD_BUTTON="${PORT_KEYBOARD_BUTTON:-none}"
export PORT_POINTER_STICK="${PORT_POINTER_STICK:-right}"
export PORT_POINTER_CLICK="${PORT_POINTER_CLICK:-r3}"
export PORT_POINTER_CLICK2="${PORT_POINTER_CLICK2:-none}"
export PORT_TOUCH_CONTROLS="${PORT_TOUCH_CONTROLS:-0}"

setup_audio() {
  local s card c server_default=0
  unset PULSE_SERVER
  for s in "${XDG_RUNTIME_DIR:-/nonexistent}/pulse/native" "/run/user/$(id -u)/pulse/native" \
           /run/user/*/pulse/native /run/pulse/native /var/run/pulse/native; do
    [ -S "$s" ] && { export PULSE_SERVER="unix:$s"; break; }
  done
  if [ -n "${PULSE_SERVER:-}" ]; then
    export SDL_AUDIODRIVER="pulseaudio,alsa"
    echo "[smmwe] audio: pulse ($PULSE_SERVER)"
    return
  fi
  export SDL_AUDIODRIVER=alsa
  if grep -qsE "pulse|pipewire" /etc/asound.conf /etc/alsa/conf.d/* \
       /usr/share/alsa/alsa.conf.d/* "$HOME/.asoundrc" 2>/dev/null; then
    server_default=1
  fi
  if [ "$server_default" = 1 ] && { pgrep -x pipewire >/dev/null 2>&1 || pgrep -x pulseaudio >/dev/null 2>&1; }; then
    server_default=2
  fi
  if [ "$server_default" = 1 ]; then
    card=""
    for c in /proc/asound/card[0-9]*; do
      [ -d "$c" ] || continue
      ls -d "$c"/pcm*p >/dev/null 2>&1 || continue
      card=${c##*/card}
      break
    done
    if [ -n "$card" ]; then
      cat > "$GAMEDIR/data/asound.conf" <<ASOUND
defaults.pcm.card $card
defaults.ctl.card $card
pcm.!default {
  type plug
  slave.pcm {
    type hw
    card $card
    device 0
  }
}
ctl.!default {
  type hw
  card $card
}
pcm.hw {
  @args [ CARD DEV ]
  @args.CARD { type string default "$card" }
  @args.DEV { type integer default 0 }
  type hw
  card \$CARD
  device \$DEV
}
ctl.hw {
  @args [ CARD ]
  @args.CARD { type string default "$card" }
  type hw
  card \$CARD
}
ASOUND
      export ALSA_CONFIG_PATH="$GAMEDIR/data/asound.conf"
    fi
  fi
  echo "[smmwe] audio: alsa (sound-server default: $server_default${card:+, card $card})"
}
setup_audio

command -v pm_platform_helper >/dev/null 2>&1 && pm_platform_helper "$GAMEDIR/$BIN"
"./$BIN" -c bin/gmloader.json &
GPID=$!
if [ "$(id -u)" = "0" ] || [ -n "$ESUDO" ]; then
  $ESUDO renice -n -4 -p "$GPID" >/dev/null 2>&1
fi
wait "$GPID"

restore_sys
command -v pm_finish >/dev/null 2>&1 && pm_finish
