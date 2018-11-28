// Copyright 2018 Yahoo Holdings. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.
package com.yahoo.jrt;

import com.yahoo.security.SslContextBuilder;
import com.yahoo.security.tls.TransportSecurityOptions;
import com.yahoo.security.tls.authz.PeerAuthorizerTrustManager.Mode;
import com.yahoo.security.tls.authz.PeerAuthorizerTrustManagersFactory;

import javax.net.ssl.SSLContext;
import javax.net.ssl.SSLEngine;
import java.nio.channels.SocketChannel;

/**
 * A {@link CryptoSocket} that creates {@link TlsCryptoSocket} instances.
 *
 * @author bjorncs
 */
public class TlsCryptoEngine implements CryptoEngine {

    private final SSLContext sslContext;

    public TlsCryptoEngine(SSLContext sslContext) {
        this.sslContext = sslContext;
    }

    public TlsCryptoEngine(TransportSecurityOptions options) {
        this(createSslContext(options));
    }

    @Override
    public TlsCryptoSocket createCryptoSocket(SocketChannel channel, boolean isServer)  {
        SSLEngine sslEngine = sslContext.createSSLEngine();
        sslEngine.setNeedClientAuth(true);
        sslEngine.setUseClientMode(!isServer);
        return new TlsCryptoSocket(channel, sslEngine);
    }

    // TODO Move to dedicated factory type controlling certificate hot-reloading in security-utils
    private static SSLContext createSslContext(TransportSecurityOptions options) {
        SslContextBuilder builder = new SslContextBuilder();
        options.getCertificatesFile()
                .ifPresent(certificates -> builder.withKeyStore(options.getPrivateKeyFile().get(), certificates));
        options.getCaCertificatesFile().ifPresent(builder::withTrustStore);
        options.getAuthorizedPeers().ifPresent(
                authorizedPeers -> builder.withTrustManagerFactory(new PeerAuthorizerTrustManagersFactory(authorizedPeers, Mode.DRY_RUN)));
        return builder.build();
    }
}
