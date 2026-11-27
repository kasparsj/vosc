import AppKit

final class HostButton: NSButton {
    private let handler: () -> Void

    init(_ title: String, handler: @escaping () -> Void) {
        self.handler = handler
        super.init(frame: .zero)
        self.title = title
        bezelStyle = .rounded
        target = self
        action = #selector(invoke)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError() }

    @objc private func invoke() { handler() }
}

final class RenderHostApplication: NSObject, NSApplicationDelegate {
    private var window: NSWindow?
    private var service: HostService?
    private var content: NSStackView?
    private var leaving = false

    func applicationDidFinishLaunching(_ notification: Notification) {
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 620, height: 420),
                              styleMask: [.titled, .closable, .miniaturizable, .resizable],
                              backing: .buffered, defer: false)
        window.title = "VOSC Render Host"
        window.minSize = NSSize(width: 500, height: 330)
        window.center()
        window.isReleasedWhenClosed = false

        let scroll = NSScrollView()
        scroll.hasVerticalScroller = true
        scroll.borderType = .noBorder
        let canvas = NSView()
        let content = NSStackView()
        content.orientation = .vertical
        content.alignment = .leading
        content.spacing = 14
        content.translatesAutoresizingMaskIntoConstraints = false
        canvas.translatesAutoresizingMaskIntoConstraints = false
        canvas.addSubview(content)
        scroll.documentView = canvas
        window.contentView = scroll
        NSLayoutConstraint.activate([
            canvas.widthAnchor.constraint(equalTo: scroll.contentView.widthAnchor),
            content.leadingAnchor.constraint(equalTo: canvas.leadingAnchor, constant: 26),
            content.trailingAnchor.constraint(lessThanOrEqualTo: canvas.trailingAnchor, constant: -26),
            content.topAnchor.constraint(equalTo: canvas.topAnchor, constant: 24),
            content.bottomAnchor.constraint(lessThanOrEqualTo: canvas.bottomAnchor, constant: -24),
            canvas.heightAnchor.constraint(greaterThanOrEqualTo: scroll.contentView.heightAnchor),
        ])
        self.content = content
        self.window = window
        service = HostService { [weak self] view in self?.render(view) }
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
    }

    private func label(_ value: String, size: CGFloat = 13, weight: NSFont.Weight = .regular) -> NSTextField {
        let field = NSTextField(wrappingLabelWithString: value)
        field.font = .systemFont(ofSize: size, weight: weight)
        field.isSelectable = true
        return field
    }

    private func fingerprint(_ value: String) -> NSTextField {
        let field = label(value, size: 12)
        field.font = .monospacedSystemFont(ofSize: 12, weight: .regular)
        return field
    }

    private func render(_ view: HostView) {
        guard let content = content else { return }
        for item in content.arrangedSubviews { content.removeArrangedSubview(item); item.removeFromSuperview() }
        content.addArrangedSubview(label(view.status, size: 20, weight: .semibold))
        let toggle = HostButton(view.active ? "Stop Draft Service" : "Enable Draft Service") { [weak self] in
            if view.active { self?.service?.disable() } else { self?.service?.enable() }
        }
        toggle.isEnabled = !view.stopping
        content.addArrangedSubview(toggle)
        if view.active, let hostFingerprint = view.fingerprint {
            let endpoint = view.port.map { "Listening on port \($0) · protocol 1" } ?? "Waiting for a local port"
            content.addArrangedSubview(label(endpoint))
            content.addArrangedSubview(label("Host certificate SHA-256", weight: .semibold))
            content.addArrangedSubview(fingerprint(hostFingerprint))
            content.addArrangedSubview(label("Draft host ID \(view.target)", size: 11))
        }
        for request in view.pending {
            content.addArrangedSubview(label("Pairing request from \(request.name)", weight: .semibold))
            content.addArrangedSubview(fingerprint(request.fingerprint))
            let approve = HostButton("Approve \(request.request.prefix(8))") { [weak self] in
                self?.service?.decide(request.request, approve: true)
            }
            let deny = HostButton("Deny \(request.request.prefix(8))") { [weak self] in
                self?.service?.decide(request.request, approve: false)
            }
            let buttons = NSStackView(views: [approve, deny])
            buttons.orientation = .horizontal
            content.addArrangedSubview(buttons)
        }
        for (key, name) in view.paired.sorted(by: { $0.key < $1.key }) {
            content.addArrangedSubview(label("Paired controller \(name)", weight: .semibold))
            content.addArrangedSubview(fingerprint(key))
            content.addArrangedSubview(HostButton("Revoke \(key.prefix(8))") { [weak self] in
                self?.service?.revoke(key)
            })
        }
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ application: NSApplication) -> Bool { true }

    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        guard let service = service, !leaving else { return .terminateNow }
        leaving = true
        service.disable { sender.reply(toApplicationShouldTerminate: true) }
        return .terminateLater
    }
}

let application = NSApplication.shared
let delegate = RenderHostApplication()
application.setActivationPolicy(.regular)
application.delegate = delegate
application.run()
