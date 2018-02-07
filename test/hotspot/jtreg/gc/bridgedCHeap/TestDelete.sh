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


# @test TestDelete.sh
# @bug 0000000
# @summary Test java.runtime.Runtime.delete(Object o)
# @run shell TestDelete.sh
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
APP_NAME=Delete

set -x

SHARED_JVM_OPTS="-XX:+UnlockDiagnosticVMOptions -XX:+TraceBridgedCHeap -XX:+TraceBridgedAlloc -XX:-UseTLAB -XX:+UseBridgedCHeap -Xlog:gc -XX:-UseCompressedOops"
EXTRA_JVM_OPTS=""
JVM_OPTS="${SHARED_JVM_OPTS} ${EXTRA_JVM_OPTS}"

# Test in delete-but-no-touch mode
cat > ${APP_NAME}.java <<EOF
public class ${APP_NAME} {
  public static void main(String[] args) {
      Object o = new Object();
      Runtime.getRuntime().delete(o);
  }
}
EOF
${JAVAC} ${APP_NAME}.java
if [ $? != 0 ]; then
  echo "Non zero return value from javac, failed"
  exit 1
fi

${JAVA} ${JVM_OPTS} ${APP_NAME}
if [ $? != 0 ]; then
  echo "Non zero return value, failed"
  exit 1
fi

# Test in delete-and-touch mode
cat > ${APP_NAME}.java <<EOF
public class ${APP_NAME} {
  public static void main(String[] args) {
      Object o = new Object();
      Runtime.getRuntime().delete(o);
      // should crash
      Class c = o.getClass();
  }
}
EOF
${JAVAC} ${APP_NAME}.java
if [ $? != 0 ]; then
  echo "Non zero return value from javac, failed"
  exit 1
fi
${JAVA} ${JVM_OPTS} ${APP_NAME}
if [ $? == 0 ]; then
  echo "Zero return value, failed"
  exit 1
fi