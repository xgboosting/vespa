// Copyright Yahoo. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.
package com.yahoo.config.provision;

import java.util.Objects;

/**
 * Represents a cloud service and its supported features.
 *
 * @author mpolden
 */
public class Cloud {

    private final CloudName name;

    private final boolean dynamicProvisioning;
    private final boolean requireAccessControl;

    private Cloud(CloudName name, boolean dynamicProvisioning, boolean requireAccessControl) {
        this.name = Objects.requireNonNull(name);
        this.dynamicProvisioning = dynamicProvisioning;
        this.requireAccessControl = requireAccessControl;
    }

    /** The name of this */
    public CloudName name() {
        return name;
    }

    /** Returns whether this can provision hosts dynamically */
    public boolean dynamicProvisioning() {
        return dynamicProvisioning;
    }

    /** Returns whether to require access control for all clusters in this */
    public boolean requireAccessControl() {
        return requireAccessControl;
    }

    /** For testing purposes only */
    public static Cloud defaultCloud() {
        return new Builder().build();
    }

    public static Builder builder() {
        return new Builder();
    }

    public static class Builder {

        private CloudName name = CloudName.DEFAULT;
        private boolean dynamicProvisioning = false;
        private boolean requireAccessControl = false;

        public Builder() {}

        public Builder name(CloudName name) {
            this.name = name;
            return this;
        }

        public Builder dynamicProvisioning(boolean dynamicProvisioning) {
            this.dynamicProvisioning = dynamicProvisioning;
            return this;
        }

        public Builder requireAccessControl(boolean requireAccessControl) {
            this.requireAccessControl = requireAccessControl;
            return this;
        }

        public Cloud build() {
            return new Cloud(name, dynamicProvisioning, requireAccessControl);
        }

    }

    @Override
    public String toString() {
        return "cloud " + name;
    }

}
