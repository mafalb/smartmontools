#!/bin/sh
# $Id$
#
# Generate ./configure from config.in and Makefile.in from Makefile.am.
# This also adds files like missing,depcomp,install-sh to the source
# directory. To update these files at a later date use:
#	autoreconf -f -i -v


# Cygwin?
test -x /usr/bin/uname && /usr/bin/uname | grep -i CYGWIN >/dev/null &&
{
    # Check for Unix text file type
    echo > dostest.tmp
    test "`wc -c < dostest.tmp`" -eq 1 ||
        echo "Warning: DOS text file type set, 'make dist' and related targets will not work."
    rm -f dostest.tmp
}

typep()
{
    cmd=$1 ; TMP=$IFS ; IFS=: ; set $PATH
    for dir
    do
	if [ -x "$dir/$cmd" ]; then
	    echo "$dir/$cmd"
	    IFS=$TMP
	    return 0
        fi
    done
    IFS=$TMP
    return 1
}

test -x "$AUTOMAKE" || AUTOMAKE=`typep automake-1.11` || AUTOMAKE=`typep automake-1.10` ||
    AUTOMAKE=`typep automake-1.9` || AUTOMAKE=`typep automake-1.8` ||
    AUTOMAKE=`typep automake-1.7` || AUTOMAKE=`typep automake17` ||
{
echo
echo "You must have at least GNU Automake 1.7 (up to 1.11) installed"
echo "in order to bootstrap smartmontools from SVN. Download the"
echo "appropriate package for your distribution, or the source tarball"
echo "from ftp://ftp.gnu.org/gnu/automake/ ."
echo
echo "Also note that support for new Automake series (anything newer"
echo "than 1.11) is only added after extensive tests. If you live in"
echo "the bleeding edge, you should know what you're doing, mainly how"
echo "to test it before the developers. Be patient."
exit 1;
}

test -x "$ACLOCAL" || ACLOCAL="aclocal`echo "$AUTOMAKE" | sed 's/.*automake//'`" && ACLOCAL=`typep "$ACLOCAL"` ||
{
echo
echo "autogen.sh found automake-1.X, but not the respective aclocal-1.X."
echo "Your installation of GNU Automake is broken or incomplete."
exit 2;
}

# Detect Automake version
case "$AUTOMAKE" in
  *automake-1.7|*automake17)
    ver=1.7 ;;
  *automake-1.8)
    ver=1.8 ;;
  *)
    ver="`$AUTOMAKE --version | sed -n '1s,^.*\([12]\.[.0-9]*[-pl0-9]*\).*$,\1,p'`"
    ver="${ver:-?.?.?}"
esac

# Warn if Automake version was not tested or does not support filesystem
case "$ver" in
  1.[78]|1.[78].*)
    # Check for case sensitive filesystem
    # (to avoid e.g. "DIST_COMMON = ... ChangeLog ..." in Makefile.in on Cygwin)
    rm -f CASETEST.TMP
    echo > casetest.tmp
    test -f CASETEST.TMP &&
    {
      echo "Warning: GNU Automake version ${ver} does not properly handle case"
      echo "insensitive filesystems. Some make targets may not work."
    }
    rm -f casetest.tmp
    ;;

  1.9.[1-6]|1.10|1.10.[12]|1.11)
    # OK
    ;;

  *)
    echo "Note: GNU Automake version ${ver} was not tested by the developers."
    echo "Please report success/failure to the smartmontools-support mailing list."
esac

set -e	# stops on error status

${ACLOCAL}
autoheader
${AUTOMAKE} --add-missing --copy --foreign
autoconf
