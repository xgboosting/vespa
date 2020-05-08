// Copyright Verizon Media. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.
package com.yahoo.vespa.hosted.provision.persistence;

import com.yahoo.component.Version;
import com.yahoo.config.provision.NodeType;
import com.yahoo.vespa.hosted.provision.os.OsVersionChange;
import org.junit.Test;

import java.nio.charset.StandardCharsets;
import java.util.Map;

import static org.junit.Assert.assertEquals;

/**
 * @author mpolden
 */
public class OsVersionChangeSerializerTest {

    @Test
    public void serialization() {
        var change = new OsVersionChange(Map.of(
                NodeType.host, Version.fromString("1.2.3"),
                NodeType.proxyhost, Version.fromString("4.5.6"),
                NodeType.confighost, Version.fromString("7.8.9")
        ));
        var serialized = OsVersionChangeSerializer.fromJson(OsVersionChangeSerializer.toJson(change));
        assertEquals(serialized, change);
    }

    @Test
    public void legacy_serialization() {
        // Read old format
        var change = new OsVersionChange(Map.of(
                NodeType.host, Version.fromString("1.2.3"),
                NodeType.proxyhost, Version.fromString("4.5.6"),
                NodeType.confighost, Version.fromString("7.8.9")
        ));
        var legacyFormat = "{\"host\":{\"version\":\"1.2.3\"},\"proxyhost\":{\"version\":\"4.5.6\"},\"confighost\":{\"version\":\"7.8.9\"}}";
        assertEquals(change, OsVersionChangeSerializer.fromJson(legacyFormat.getBytes(StandardCharsets.UTF_8)));

        // Write format supported by both old and new serializer
        var oldFormat = "{\"targets\":[{\"nodeType\":\"host\",\"version\":\"1.2.3\"}," +
                        "{\"nodeType\":\"proxyhost\",\"version\":\"4.5.6\"}," +
                        "{\"nodeType\":\"confighost\",\"version\":\"7.8.9\"}]," +
                        "\"host\":{\"version\":\"1.2.3\"}," +
                        "\"proxyhost\":{\"version\":\"4.5.6\"}," +
                        "\"confighost\":{\"version\":\"7.8.9\"}}";
        assertEquals(oldFormat, new String(OsVersionChangeSerializer.toJson(change), StandardCharsets.UTF_8));
    }

}
