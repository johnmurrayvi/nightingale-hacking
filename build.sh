#!/bin/bash

# break on any error
set -e

# do a debug or a release build? YOU WANT RELEASE!
# if you do a debug build, add the appropriate options
# to your nightingale.config file!
build="release"
buildir="$(pwd)"
version=xul9.0.1

# This variable is used to look into the mozilla dependencies to see
# which versions (build/release) have been extracted.
mozdepver="mozilla-9.0.1"

count=`grep '\-\-enable\-debug' nightingale.config|wc -l`
if [ $count -ne 0 ] ; then
    build="debug"
fi

echo "You are building a $build build"

download() {
    if which wget &>/dev/null ; then
        wget "$1"
    elif which curl &>/dev/null ; then
        curl --location -o "${1##*/}" "$1"
    else
        echo "Failed to find downloader to fetch $1" >&2
        exit 1
    fi
}

md5_verify() {
    md5_fail() {
        echo "-------------------------------------------------------------------------------"
        echo "WARNING: MD5 checksum verification failed: $1"
        echo "It is a safety risk to continue unless you know exactly what you're doing."
        echo "Answer no to delete it and retry the build process, or yes to continue anyway."
        read -p "Continue? (y/n) [n]" ans
        case $ans in
            "y" | "Y")
                echo "Checksum ignored."
                ;;
            "n" | "N" | "")
                rm "$1"
                exit 1
                ;;
            *)
                echo "Invalid input."
                md5_fail $1
                ;;
        esac
    }

    if [ $depdirn = "macosx-i686" ] ; then
        md5 -r "$1" | grep -q -f "$1.md5" || md5_fail "$1"
    else
        md5sum -c --status "$1.md5" || md5_fail "$1"
    fi
}

# Check for the build deps for the system's architecture and OS
case $OSTYPE in
    linux*)
        case "$(uname -m)" in
            *64*) arch=x86_64 ;;
            *86)    arch=i686 ;;
            *) echo "Unknown arch" >&2 ; exit 1 ;;
        esac
        depdirn="linux-$arch"
        #if you have a dep built on a differing date for either arch, just use a conditional to set this
        if [ "$arch" = "i686" ] ; then
            depdate=20140302
        else
            depdate=20140214 # TODO: with gst-1, use 20140214 deps!
        fi
        fname="$depdirn-$version-$depdate-$build.tar.bz2"

        #export CXXFLAGS="-O2 -fomit-frame-pointer -pipe -fpermissive"

        echo "linux $arch"
        ( cd dependencies && {
            if [ ! -f "$fname" ] ; then
                    download "http://downloads.sourceforge.net/project/ngale/$version-Build-Deps/$depdirn/$fname"
                    download "http://downloads.sourceforge.net/project/ngale/$version-Build-Deps/$depdirn/$fname.md5"
            fi
            if [ ! -d "$depdirn/$mozdepver/$build" ] ; then
                if [ -f "$fname.md5" ] ; then
                    md5_verify "$fname"
                fi
                echo "Need to extract $fname"
                tar xvf "$fname"
            fi
        } ; )

        # https://en.wikipedia.org/wiki/List_of_Linux_distributions#Debian-based
        debianbased="buntu|Debian|LMDE|Mint|gNewSense|Fuduntu|Solus|CrunchBang|Peppermint|Deepin|Kali|Trisquel|elementary|Knoppix"
        # the below needs to be nested...in my testing it won't work otherwise
        if [[ $(grep -i -E $debianbased /etc/issue) ||
              $(grep -i -E $debianbased /etc/lsb-release) ||
              $(grep -i -E $debianbased /etc/os-release) ]] ; then
            grep -q -E 'taglib' nightingale.config || \
            echo -e 'ac_add_options --with-taglib-source=packaged\n' >> nightingale.config
        fi
        ;;
    msys*)
        depdirn="windows-i686"
        # Nightingale version number and dependency version, change if the deps change.
        depdate=20140404
        msvcver="msvc10"

        # Ensure line endings, as git might have converted them
        tr -d '\r' < ./components/library/localdatabase/content/schema.sql > tmp.sql
        rm ./components/library/localdatabase/content/schema.sql
        mv tmp.sql ./components/library/localdatabase/content/schema.sql

        cd dependencies

        fname="$depdirn-$version-$msvcver-$depdate.tar.bz2"

        if [ ! -f "$fname" ] ; then
            download "http://downloads.sourceforge.net/project/ngale/$version-Build-Deps/$depdirn/$fname"
            download "http://downloads.sourceforge.net/project/ngale/$version-Build-Deps/$depdirn/$fname.md5"
        fi

        if [ ! -d "$depdirn/$mozdepver/$build" ] ; then
            if [ -f "$fname.md5" ] ; then
                md5_verify "$fname"
            fi
            mkdir "$depdirn"
            tar -xvf "$fname" -C "$depdirn"
        fi
        cd ../
        ;;
    darwin*)
        depdirn="macosx-i686"
        depdate=20140324
        arch_flags="-m32 -arch i386"
        export CFLAGS="$arch_flags"
        export CXXFLAGS="$arch_flags"
        export CPPFLAGS="$arch_flags"
        export LDFLAGS="$arch_flags"
        export OBJCFLAGS="$arch_flags"

        echo 'ac_add_options --with-macosx-sdk=/Developer/SDKs/MacOSX10.5.sdk' > nightingale.config
        echo 'ac_add_options --enable-installer' >> nightingale.config
        echo 'ac_add_options --enable-official' >> nightingale.config
        echo 'ac_add_options --enable-compiler-environment-checks=no' >> nightingale.config

        cd dependencies

        fname="$depdirn-$version-$depversion-$build.tar.bz2"

        if [ ! -f "$fname" ] ; then
            download "http://downloads.sourceforge.net/project/ngale/$version-Build-Deps/$depdirn/$fname"
            download "http://downloads.sourceforge.net/project/ngale/$version-Build-Deps/$depdirn/$fname.md5"
        fi

        if [ ! -d "$depdirn/$mozdepver/$build" ] ; then
            if [ -f "$fname.md5" ] ; then
                md5_verify "$fname"
            fi
            tar xvf "$fname"
        fi

        cd ../
        ;;
    *)
        echo "Can't find deps for $OSTYPE. You may need to build them yourself. Doublecheck the SVN's for \n
        Nightingale trunk to be sure."
        ;;
esac

# # get the vendor build deps...
# cd dependencies

# if [ ! -f "vendor-$version.zip" ] ; then
#   download "http://downloads.sourceforge.net/project/ngale/$version-Build-Deps/vendor-$version.zip"
#   md5_verify "vendor-$version.zip"
# fi

# if [ ! -d "vendor" ] ; then
#   rm -rf vendor &> /dev/null
#   unzip "vendor-$version.zip"
# fi

cd ../
cd $buildir

make clobber
rm -rf compiled &> /dev/null #sometimes clobber doesn't nuke it all
make

echo "Build Succeeded!"
