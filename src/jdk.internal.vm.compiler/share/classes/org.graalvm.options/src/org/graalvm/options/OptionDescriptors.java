/*
 * Copyright (c) 2017, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */
package org.graalvm.options;

import java.util.Arrays;
import java.util.Collections;
import java.util.Iterator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.NoSuchElementException;

/**
 * An interface to a set of {@link OptionDescriptor}s.
 *
 * @since 1.0
 */
public interface OptionDescriptors extends Iterable<OptionDescriptor> {

    /**
     * An empty set of option descriptors.
     *
     * @since 1.0
     */
    OptionDescriptors EMPTY = new OptionDescriptors() {

        public Iterator<OptionDescriptor> iterator() {
            return Collections.<OptionDescriptor> emptyList().iterator();
        }

        public OptionDescriptor get(String key) {
            return null;
        }
    };

    /**
     * Gets the {@link OptionDescriptor} matching a given option name or {@code null} if this option
     * descriptor set does not contain a matching option name.
     *
     * @since 1.0
     */
    OptionDescriptor get(String optionName);

    /**
     * Creates a union options descriptor out of multiple given descriptors. The operation
     * descriptors are not checked for duplicate keys. The option descriptors are iterated in
     * declaration order.
     *
     * @since 1.0
     */
    static OptionDescriptors createUnion(OptionDescriptors... descriptors) {
        if (descriptors.length == 0) {
            return EMPTY;
        } else if (descriptors.length == 1) {
            return descriptors[0];
        } else {
            return new UnionOptionDescriptors(descriptors);
        }
    }

    /**
     * {@inheritDoc}
     *
     * @since 1.0
     */
    @Override
    Iterator<OptionDescriptor> iterator();

    /**
     * Creates an {@link OptionDescriptors} instance from a list. The option descriptors
     * implementation is backed by a {@link LinkedHashMap} that preserves ordering.
     *
     * @since 1.0
     */
    static OptionDescriptors create(List<OptionDescriptor> descriptors) {
        if (descriptors == null || descriptors.isEmpty()) {
            return EMPTY;
        }
        return new OptionDescriptorsMap(descriptors);
    }
}

class OptionDescriptorsMap implements OptionDescriptors {

    final Map<String, OptionDescriptor> descriptors = new LinkedHashMap<>();

    OptionDescriptorsMap(List<OptionDescriptor> descriptorList) {
        for (OptionDescriptor descriptor : descriptorList) {
            descriptors.put(descriptor.getName(), descriptor);
        }
    }

    @Override
    public OptionDescriptor get(String optionName) {
        return descriptors.get(optionName);
    }

    @Override
    public Iterator<OptionDescriptor> iterator() {
        return descriptors.values().iterator();
    }

}

final class UnionOptionDescriptors implements OptionDescriptors {

    final OptionDescriptors[] descriptorsList;

    UnionOptionDescriptors(OptionDescriptors[] descriptors) {
        // defensive copy
        this.descriptorsList = Arrays.copyOf(descriptors, descriptors.length);
    }

    public Iterator<OptionDescriptor> iterator() {
        return new Iterator<OptionDescriptor>() {

            Iterator<OptionDescriptor> descriptors = descriptorsList[0].iterator();
            int descriptorsIndex = 0;
            OptionDescriptor next = null;

            public boolean hasNext() {
                return fetchNext() != null;
            }

            private OptionDescriptor fetchNext() {
                if (next != null) {
                    return next;
                }
                if (descriptors.hasNext()) {
                    next = descriptors.next();
                    return next;
                } else if (descriptorsIndex < descriptorsList.length - 1) {
                    descriptorsIndex++;
                    descriptors = descriptorsList[descriptorsIndex].iterator();
                    return fetchNext();
                } else {
                    return null;
                }
            }

            public OptionDescriptor next() {
                OptionDescriptor fetchedNext = fetchNext();
                if (fetchedNext != null) {
                    // consume next
                    this.next = null;
                    return fetchedNext;
                } else {
                    throw new NoSuchElementException();
                }
            }
        };
    }

    public OptionDescriptor get(String value) {
        for (OptionDescriptors descriptors : descriptorsList) {
            OptionDescriptor descriptor = descriptors.get(value);
            if (descriptor != null) {
                return descriptor;
            }
        }
        return null;
    }

}
