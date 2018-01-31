#
#  Copyright (c) 2018, Oracle and/or its affiliates. All rights reserved.
#  DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
#
#  This code is free software; you can redistribute it and/or modify it
#  under the terms of the GNU General Public License version 2 only, as
#  published by the Free Software Foundation.
#
#  This code is distributed in the hope that it will be useful, but WITHOUT
#  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
#  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
#  version 2 for more details (a copy is included in the LICENSE file that
#  accompanied this code).
#
#  You should have received a copy of the GNU General Public License version
#  2 along with this work; if not, write to the Free Software Foundation,
#  Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
#
#  Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
#  or visit www.oracle.com if you need additional information or have any
#  questions.
#


# @test TestUseBridgedCHeap.sh
# @bug 0000000
# @summary Test behavior of bridged CHeap related options
# @run shell TestUseBridgedCHeap.sh
#

if [ "${TESTSRC}" = "" ]
then
  TESTSRC=${PWD}
  echo "TESTSRC not set.  Using "${TESTSRC}" as default"
fi
echo "TESTSRC=${TESTSRC}"
## Adding common setup Variables for running shell tests.
. ${TESTSRC}/../../test_env.sh

JAVA=${TESTJAVA}${FS}bin${FS}java
JAVAC=${TESTJAVA}${FS}bin${FS}javac

# Test with `java -version`
OUTPUT=$(${JAVA} -XX:+UseBridgedCHeap -Xlog:gc -XX:-UseCompressedOops -version)
if [ $? != 0 ]; then
  echo "Non zero return value, failed"
  exit 1
fi

if [ -z "$(echo ${OUTPUT}) | grep '[info][gc] Using Bridged C-Heap'" ]; then
  echo "Expected GC log message not found"
  exit 1
fi

# Test with `HelloWorld` app
cat >> Hello.java <<EOF
public class Hello {
  public static void main(String[] args) {
    System.out.println("HelloWorld");
  }
}
EOF
${JAVAC} Hello.java
OUTPUT=$(${JAVA} -XX:+UseBridgedCHeap -Xlog:gc -XX:-UseCompressedOops Hello)
if [ $? != 0 ]; then
  echo "Non zero return value, failed"
  exit 1
fi
if [ -z "$(echo ${OUTPUT}) | grep HelloWorld" ]; then
  echo "Expected message not found"
  exit 1
fi
