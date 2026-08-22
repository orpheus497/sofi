#!/usr/bin/env bash

# Various options for the file browser script:
SOFI_FB_GENERIC_FO="xdg-open" # command used for opening the selection
SOFI_FB_PREV_LOC_FILE=~/.local/share/sofi/sofi_fb_prevloc
SOFI_FB_HISTORY_FILE=~/.local/share/sofi/sofi_fb_history
SOFI_FB_HISTORY_MAXCOUNT=5 # maximum number of history entries
# Comment the next variable to always start in the last visited directory,
# otherwise sofi_fb will start in the specified directory:
SOFI_FB_START_DIR=$HOME # starting directory
# Uncomment the following line to disable history:
# SOFI_FB_NO_HISTORY=1

# Beginning of the script:
# Create the directory for the files of the script
if [ ! -d "$(dirname "${SOFI_FB_PREV_LOC_FILE}")" ]
then
    mkdir -p "$(dirname "${SOFI_FB_PREV_LOC_FILE}")"
fi
if [ ! -d "$(dirname "${SOFI_FB_HISTORY_FILE}")" ]
then
    mkdir -p "$(dirname "${SOFI_FB_HISTORY_FILE}")"
fi

# Initialize $SOFI_FB_CUR_DIR
if [ -d "${SOFI_FB_START_DIR}" ]
then
    SOFI_FB_CUR_DIR="${SOFI_FB_START_DIR}"
else
    SOFI_FB_CUR_DIR="$PWD"
fi

# Read last location, otherwise we default to $SOFI_FB_START_DIR or $PWD.
if [ -f "${SOFI_FB_PREV_LOC_FILE}" ]
then
    SOFI_FB_CUR_DIR=$(cat "${SOFI_FB_PREV_LOC_FILE}")
fi

# Handle argument.
if [ -n "$@" ]
then
    if [[ "$@" == /* ]]
    then
        SOFI_FB_CUR_DIR="$@"
    else
        SOFI_FB_CUR_DIR="${SOFI_FB_CUR_DIR}/$@"
    fi
fi

# If argument is no directory.
if [ ! -d "${SOFI_FB_CUR_DIR}" ]
then
    if [ -x "${SOFI_FB_CUR_DIR}" ]
    then
        coproc ( "${SOFI_FB_CUR_DIR}" >/dev/null 2>&1 )
        exec 1>&-
        exit;
    elif [ -f "${SOFI_FB_CUR_DIR}" ]
    then
        if [[ "${SOFI_FB_NO_HISTORY}" -ne 1 ]]
        then
            # Append selected entry to history and remove exceeding entries
            sed -i "s|${SOFI_FB_CUR_DIR}|##deleted##|g" "${SOFI_FB_HISTORY_FILE}"
            sed -i '/##deleted##/d' "${SOFI_FB_HISTORY_FILE}"
            echo "${SOFI_FB_CUR_DIR}" >> "${SOFI_FB_HISTORY_FILE}"
            if [ "$( wc -l < "${SOFI_FB_HISTORY_FILE}" )" -gt "${SOFI_FB_HISTORY_MAXCOUNT}" ]
            then
                sed -i 1d "${SOFI_FB_HISTORY_FILE}"
            fi
        fi
        # Open the selected entry with $SOFI_FB_GENERIC_FO
        coproc ( "${SOFI_FB_GENERIC_FO}" "${SOFI_FB_CUR_DIR}" >/dev/null 2>&1 )
        if [ -d "${SOFI_FB_START_DIR}" ]
        then
            echo "${SOFI_FB_START_DIR}" > "${SOFI_FB_PREV_LOC_FILE}"
        fi
        exit;
    fi
    exit;
fi

# Process current dir.
if [ -n "${SOFI_FB_CUR_DIR}" ]
then
    SOFI_FB_CUR_DIR=$(readlink -e "${SOFI_FB_CUR_DIR}")
    echo "${SOFI_FB_CUR_DIR}" > "${SOFI_FB_PREV_LOC_FILE}"
    pushd "${SOFI_FB_CUR_DIR}" >/dev/null
fi

# Output to sofi
if [[ "${SOFI_FB_NO_HISTORY}" -ne 1 ]]
then
    tac "${SOFI_FB_HISTORY_FILE}" | grep "${SOFI_FB_CUR_DIR}"
fi
echo ".."
ls
# vim:sw=4:ts=4:et:
