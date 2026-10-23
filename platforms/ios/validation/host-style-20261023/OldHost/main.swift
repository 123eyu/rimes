import UIKit
// Minimal host: one text view. Linked with an older SDK version to get the legacy keyboard.
final class VC: UIViewController {
    let field = UITextView()
    override func viewDidLoad() {
        super.viewDidLoad(); view.backgroundColor = .systemBackground
        field.frame = CGRect(x: 20, y: 120, width: 350, height: 200); field.font = .systemFont(ofSize: 20)
        field.accessibilityIdentifier = "oldhost.field"; field.layer.borderWidth = 1
        view.addSubview(field)
    }
    override func viewDidAppear(_ animated: Bool) { super.viewDidAppear(animated); field.becomeFirstResponder() }
}
final class AppDelegate: UIResponder, UIApplicationDelegate {
    var window: UIWindow?
    func application(_ a: UIApplication, didFinishLaunchingWithOptions o: [UIApplication.LaunchOptionsKey: Any]?) -> Bool {
        window = UIWindow(frame: UIScreen.main.bounds); window?.rootViewController = VC(); window?.makeKeyAndVisible(); return true
    }
}
UIApplicationMain(CommandLine.argc, CommandLine.unsafeArgv, nil, NSStringFromClass(AppDelegate.self))
