import Foundation
import Network
import Darwin

struct Endpoint: Codable { let host: String; let port: Int }
struct Peer: Codable { let endpoint: Endpoint; let target: String; let name: String }
struct Discovery: Codable { let peers: [Peer]; let error: String? }

final class Browse: NSObject, NetServiceDelegate {
    private let browser = NWBrowser(for: .bonjourWithTXTRecord(type: "_vosc-draft._tcp", domain: "local."),
                                    using: .tcp)
    private var resolving: [NWEndpoint: NetService] = [:]
    private var advertisedRecords: [NetService: [String: String]] = [:]
    private var peers: [String: Peer] = [:]
    private var failure: String?

    func start() {
        browser.stateUpdateHandler = { [weak self] state in
            switch state {
            case .waiting(let error), .failed(let error):
                self?.failure = Self.message(error)
                if self?.failure == "macOS denied local network access." { self?.finish() }
            case .ready:
                self?.failure = nil
            default:
                break
            }
        }
        browser.browseResultsChangedHandler = { [weak self] results, _ in
            guard let self else { return }
            for result in results where self.resolving[result.endpoint] == nil && self.resolving.count < 32 {
                guard case .service(let name, let type, let domain, _) = result.endpoint,
                      case .bonjour(let record) = result.metadata else { continue }
                let fields = record.dictionary
                guard fields["role"] == "draft", fields["version"] == "1",
                      let target = fields["target"], UUID(uuidString: target) != nil else { continue }
                let service = NetService(domain: domain, type: type, name: name)
                self.advertisedRecords[service] = fields
                self.resolving[result.endpoint] = service
                service.delegate = self
                service.resolve(withTimeout: 2)
            }
        }
        browser.start(queue: .main)
        DispatchQueue.main.asyncAfter(deadline: .now() + 4) { [weak self] in self?.finish() }
    }

    private static func message(_ error: NWError) -> String {
        switch error {
        case .dns(let code) where code == kDNSServiceErr_PolicyDenied:
            return "macOS denied local network access."
        case .posix(.EACCES), .posix(.EPERM):
            return "macOS denied local network access."
        default:
            return "Bonjour discovery is unavailable."
        }
    }

    func netServiceDidResolveAddress(_ sender: NetService) {
        guard let target = advertisedRecords[sender]?["target"],
              let host = sender.hostName, (1...65535).contains(sender.port) else { return }
        peers[target] = Peer(endpoint: Endpoint(host: host, port: sender.port), target: target, name: sender.name)
    }

    private func finish() {
        browser.cancel()
        for service in resolving.values { service.stop() }
        let report = Discovery(peers: peers.values.sorted { $0.target < $1.target }, error: failure)
        if let output = try? JSONEncoder().encode(report) { FileHandle.standardOutput.write(output) }
        exit(EXIT_SUCCESS)
    }
}

let browse = Browse()
browse.start()
RunLoop.main.run()
