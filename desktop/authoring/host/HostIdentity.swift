import CryptoKit
import Foundation
import Security

func sha256Hex(_ bytes: Data) -> String {
    SHA256.hash(data: bytes).map { String(format: "%02x", $0) }.joined()
}

struct HostIdentity {
    let identity: SecIdentity
    let certificate: Data
    let fingerprint: String

    private static let keyTag = Data("com.vosc.render-host.identity.p256.v1".utf8)
    private static let certificateService = "com.vosc.render-host.certificate.v1"

    static func load() throws -> Self {
        var key: CFTypeRef?
        let keyLookup: WireObject = [
            kSecClass as String: kSecClassKey,
            kSecAttrApplicationTag as String: keyTag,
            kSecAttrKeyType as String: kSecAttrKeyTypeECSECPrimeRandom,
            kSecReturnRef as String: true,
        ]
        let lookup = SecItemCopyMatching(keyLookup as CFDictionary, &key)
        let privateKey: SecKey
        if lookup == errSecItemNotFound {
            var error: Unmanaged<CFError>?
            let attributes: WireObject = [
                kSecAttrKeyType as String: kSecAttrKeyTypeECSECPrimeRandom,
                kSecAttrKeySizeInBits as String: 256,
                kSecPrivateKeyAttrs as String: [
                    kSecAttrIsPermanent as String: true,
                    kSecAttrIsExtractable as String: false,
                    kSecAttrApplicationTag as String: keyTag,
                ],
            ]
            guard let created = SecKeyCreateRandomKey(attributes as CFDictionary, &error) else {
                throw WireError("keychain", "Could not create the render-host identity: \(error?.takeRetainedValue().localizedDescription ?? "unknown Keychain error")")
            }
            privateKey = created
        } else {
            guard lookup == errSecSuccess, let key = key, CFGetTypeID(key) == SecKeyGetTypeID() else {
                throw WireError("keychain", "Could not open the render-host identity in Keychain (\(lookup))")
            }
            privateKey = key as! SecKey
        }

        let item: WireObject = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: certificateService,
            kSecAttrAccount as String: "public-leaf",
        ]
        var publicItem: CFTypeRef?
        var query = item
        query[kSecReturnData as String] = true
        let certificateLookup = SecItemCopyMatching(query as CFDictionary, &publicItem)
        let bytes: Data
        if certificateLookup == errSecItemNotFound {
            bytes = try makeCertificate(privateKey)
            var saved = item
            saved[kSecValueData as String] = bytes
            let result = SecItemAdd(saved as CFDictionary, nil)
            guard result == errSecSuccess else {
                throw WireError("keychain", "Could not save the render-host certificate (\(result))")
            }
        } else {
            guard certificateLookup == errSecSuccess, let stored = publicItem as? Data else {
                throw WireError("keychain", "Could not open the render-host certificate (\(certificateLookup))")
            }
            bytes = stored
        }
        guard let certificate = SecCertificateCreateWithData(nil, bytes as CFData),
              let certificateKey = SecCertificateCopyKey(certificate),
              let expectedKey = SecKeyCopyPublicKey(privateKey),
              publicBytes(certificateKey) == publicBytes(expectedKey),
              let identity = SecIdentityCreate(nil, certificate, privateKey) else {
            throw WireError("keychain", "The stored render-host certificate and Keychain key do not match")
        }
        return HostIdentity(identity: identity, certificate: bytes, fingerprint: sha256Hex(bytes))
    }

    private static func publicBytes(_ key: SecKey) -> Data? {
        SecKeyCopyExternalRepresentation(key, nil) as Data?
    }

    private static func makeCertificate(_ privateKey: SecKey) throws -> Data {
        guard let key = SecKeyCopyPublicKey(privateKey), let publicKey = publicBytes(key) else {
            throw WireError("keychain", "Could not read the host public key")
        }
        let ecdsaSHA256 = DER.sequence(DER.oid([0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03, 0x02]))
        let publicKeyAlgorithm = DER.sequence(
            DER.oid([0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01]),
            DER.oid([0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07])
        )
        let name = DER.sequence(DER.element(0x31, DER.sequence(
            DER.oid([0x55, 0x04, 0x03]), DER.element(0x0c, Data("vosc-draft".utf8))
        )))
        let now = Date()
        let validity = DER.sequence(DER.time(now.addingTimeInterval(-86400)),
                                    DER.time(now.addingTimeInterval(3650 * 86400)))
        let serial = Data(UUID().uuidString.replacingOccurrences(of: "-", with: "").utf8)
        let keyUsage = DER.sequence(DER.oid([0x55, 0x1d, 0x0f]), DER.element(0x01, Data([0xff])),
                                    DER.element(0x04, DER.element(0x03, Data([7, 0x80]))))
        let subjectAltName = DER.sequence(DER.oid([0x55, 0x1d, 0x11]),
            DER.element(0x04, DER.sequence(DER.element(0x82, Data("vosc-draft".utf8)))))
        let tbs = DER.sequence(
            DER.element(0xa0, DER.element(0x02, Data([2]))),
            DER.element(0x02, Data(serial.prefix(16))), ecdsaSHA256, name, validity, name,
            DER.sequence(publicKeyAlgorithm, DER.bitString(publicKey)),
            DER.element(0xa3, DER.sequence(keyUsage, subjectAltName))
        )
        var error: Unmanaged<CFError>?
        guard let signature = SecKeyCreateSignature(privateKey, .ecdsaSignatureMessageX962SHA256,
                                                     tbs as CFData, &error) as Data? else {
            throw WireError("keychain", "Could not sign the host certificate")
        }
        return DER.sequence(tbs, ecdsaSHA256, DER.bitString(signature))
    }
}

private enum DER {
    static func element(_ tag: UInt8, _ bytes: Data) -> Data {
        var header = Data([tag])
        if bytes.count < 128 {
            header.append(UInt8(bytes.count))
        } else {
            let length = bytes.count
            let encoded = length < 256 ? [UInt8(length)] : [UInt8(length >> 8), UInt8(length & 255)]
            header.append(0x80 | UInt8(encoded.count))
            header.append(contentsOf: encoded)
        }
        return header + bytes
    }

    static func sequence(_ elements: Data...) -> Data { element(0x30, elements.reduce(Data(), +)) }
    static func oid(_ bytes: [UInt8]) -> Data { element(0x06, Data(bytes)) }
    static func bitString(_ bytes: Data) -> Data { element(0x03, Data([0]) + bytes) }

    static func time(_ date: Date) -> Data {
        let format = DateFormatter()
        format.locale = Locale(identifier: "en_US_POSIX")
        format.timeZone = TimeZone(secondsFromGMT: 0)
        format.dateFormat = "yyyyMMddHHmmss'Z'"
        return element(0x18, Data(format.string(from: date).utf8))
    }
}
