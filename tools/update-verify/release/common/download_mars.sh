#!/bin/bash

download_mars () {
    update_url="$1"
    only="$2"
    test_only="$3"
    to_build_id="$4"
    to_app_version="$5"
    to_display_version="$6"

    max_tries=5
    try=1
    # retrying until we get offered an update
    while [ "$try" -le "$max_tries" ]; do
        echo "Using  $update_url"
        # retrying until AUS gives us any response at all
        cached_download update.xml "${update_url}"

        echo "Got this response:"
        cat update.xml
        # If the first line after <updates> is </updates> then we have an
        # empty snippet. Otherwise we're done
        if [ "$(grep -A1 '<updates>' update.xml | tail -1)" != "</updates>" ]; then
            break;
        fi
        echo "Empty response, sleeping"
        sleep 5
        try=$((try+1))
    done

    echo; echo;  # padding

    update_line=$(grep -F "<update " update.xml)
    grep_rv=$?
    if [ 0 -ne $grep_rv ]; then
        echo "TEST-UNEXPECTED-FAIL: no <update/> found for $update_url"
        return 1
    fi
    command=$(echo "$update_line" | sed -e 's/^.*<update //' -e 's:>.*$::' -e 's:\&amp;:\&:g')
    eval "export $command"

    # buildID and some other variables further down are gathered by eval'ing
    # the massaged `update.xml` file a bit further up. Because of this, shellcheck
    # cannot verify their existence, and gets grumpy. Ideally we would do this
    # differently, but it's not worth the trouble at the time of writing.
    # shellcheck disable=SC2154
    if [ -n "$to_build_id" ] && [ "$buildID" != "$to_build_id" ]; then
        echo "TEST-UNEXPECTED-FAIL: expected buildID $to_build_id does not match actual $buildID"
        return 1
    fi

    # shellcheck disable=SC2154
    if [ -n "$to_display_version" ] && [ "$displayVersion" != "$to_display_version" ]; then
        echo "TEST-UNEXPECTED-FAIL: expected displayVersion $to_display_version does not match actual $displayVersion"
        return 1
    fi

    # shellcheck disable=SC2154
    if [ -n "$to_app_version" ] && [ "$appVersion" != "$to_app_version" ]; then
        echo "TEST-UNEXPECTED-FAIL: expected appVersion $to_app_version does not match actual $appVersion"
        return 1
    fi

    mkdir -p update/
    if [ -z "$only" ]; then
      only="partial complete"
    fi
    for patch_type in $only
      do
      line=$(grep -F "patch type=\"$patch_type" update.xml)
      grep_rv=$?

      if [ 0 -ne $grep_rv ]; then
        echo "TEST-UNEXPECTED-FAIL: no $patch_type update found for $update_url"
        return 1
      fi

      command=$(echo "$line" | sed -e 's/^.*<patch //' -e 's:/>.*$::' -e 's:\&amp;:\&:g')
      eval "export $command"

      if [ "$test_only" == "1" ]
      then
        echo "Testing $URL"
        curl -s -I -L "$URL"
        return
      else
        if ! cached_download "update/${patch_type}.mar" "${URL}"; then
          echo "Could not download $patch_type!"
          echo "from: $URL"
        fi
      fi
      actual_size=$(perl -e "printf \"%d\n\", (stat(\"update/$patch_type.mar\"))[7]")
      # shellcheck disable=SC2154
      actual_hash=$(openssl dgst -"$hashFunction" update/"$patch_type".mar | sed -e 's/^.*= //')

      # shellcheck disable=SC2154
      if [ "$actual_size" != "$size" ]; then
          echo "TEST-UNEXPECTED-FAIL: $patch_type from $update_url wrong size"
          # shellcheck disable=SC2154
          echo "TEST-UNEXPECTED-FAIL: update.xml size: $size"
          echo "TEST-UNEXPECTED-FAIL: actual size: $actual_size"
          return 1
      fi

      # shellcheck disable=SC2154
      if [ "$actual_hash" != "$hashValue" ]; then
          echo "TEST-UNEXPECTED-FAIL: $patch_type from $update_url wrong hash"
          # shellcheck disable=SC2154
          echo "TEST-UNEXPECTED-FAIL: update.xml hash: $hashValue"
          echo "TEST-UNEXPECTED-FAIL: actual hash: $actual_hash"
          return 1
      fi

      cp update/"$patch_type".mar update/update.mar
      echo "$actual_size" > update/"$patch_type".size

    done
}
