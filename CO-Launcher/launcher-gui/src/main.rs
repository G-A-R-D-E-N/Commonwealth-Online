#![cfg_attr(all(windows, not(debug_assertions)), windows_subsystem = "windows")]

use std::borrow::Cow;
use std::fs;
use std::path::{Path, PathBuf};

use launcher_gui::assets;
use launcher_gui::mods;
use launcher_gui::protocol::{self, Command, Event};
use launcher_gui::shell::{Job, Shell, Step};
use tao::dpi::LogicalSize;
use tao::event::{Event as TaoEvent, WindowEvent};
use tao::event_loop::{ControlFlow, EventLoopBuilder, EventLoopProxy};
use tao::window::{Window, WindowBuilder};
use wry::http::header::CONTENT_TYPE;
use wry::http::{Request, Response};
use wry::{WebContext, WebView, WebViewBuilder};

const START_URL: &str = if cfg!(windows) {
    "http://co.localhost/index.html"
} else {
    "co://localhost/index.html"
};

#[derive(Debug)]
enum UserEvent {
    Ipc(Command),
    Emit(Box<Event>),
}

struct Host {
    _window: Window,
    webview: WebView,
    shell: Shell,
    proxy: EventLoopProxy<UserEvent>,
}

impl Host {
    fn deliver(&mut self, event: &Event) {
        self.shell.observe(event);
        let _ = self.webview.evaluate_script(&protocol::script(event));
    }

    fn apply(&mut self, step: Step, control_flow: &mut ControlFlow) {
        match step {
            Step::Emit(event) => self.deliver(&event),
            Step::Work(job) => self.spawn(job),
            Step::Reveal(path) => reveal(&path),
            Step::Quit => *control_flow = ControlFlow::Exit,
        }
    }

    fn spawn(&self, job: Job) {
        let proxy = self.proxy.clone();
        std::thread::spawn(move || {
            job.run(&|event| {
                let _ = proxy.send_event(UserEvent::Emit(Box::new(event)));
            })
        });
    }
}

fn reveal(path: &Path) {
    if cfg!(windows) {
        let _ = std::process::Command::new("explorer")
            .arg("/select,")
            .arg(path)
            .spawn();
    } else if let Some(folder) = path.parent() {
        let _ = open::that(folder);
    }
}

fn serve(request: Request<Vec<u8>>) -> Response<Cow<'static, [u8]>> {
    match assets::lookup(request.uri().path()) {
        Some((mime, bytes)) => Response::builder()
            .header(CONTENT_TYPE, mime)
            .body(Cow::Borrowed(bytes))
            .expect("static response headers are valid"),
        None => Response::builder()
            .status(404)
            .body(Cow::Borrowed(&[][..]))
            .expect("static response headers are valid"),
    }
}

fn navigation_allowed(url: &str) -> bool {
    url.starts_with("http://co.localhost/") || url.starts_with("co://")
}

fn app_dir() -> Option<PathBuf> {
    dirs::data_local_dir().map(|dir| dir.join("CommonwealthOnline"))
}

fn fail(message: &str) -> ! {
    if let Some(dir) = app_dir() {
        let _ = fs::create_dir_all(&dir);
        let _ = fs::write(dir.join("launcher-gui-error.txt"), message);
    }
    std::process::exit(1)
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let event_loop = EventLoopBuilder::<UserEvent>::with_user_event().build();
    let window = WindowBuilder::new()
        .with_title("Commonwealth Online Launcher")
        .with_inner_size(LogicalSize::new(1280.0, 720.0))
        .with_min_inner_size(LogicalSize::new(960.0, 600.0))
        .with_resizable(true)
        .build(&event_loop)?;
    let ipc_proxy = event_loop.create_proxy();
    let mut web_context = WebContext::new(app_dir().map(|dir| dir.join("launcher-gui-webview")));
    let webview = match WebViewBuilder::new_with_web_context(&mut web_context)
        .with_custom_protocol("co".to_owned(), |_, request| serve(request))
        .with_url(START_URL)
        .with_devtools(cfg!(debug_assertions))
        .with_drag_drop_handler(|_| true)
        .with_navigation_handler(|url| navigation_allowed(&url))
        .with_ipc_handler(move |request: Request<String>| {
            if let Ok(command) = serde_json::from_str::<Command>(request.body()) {
                let _ = ipc_proxy.send_event(UserEvent::Ipc(command));
            }
        })
        .build(&window)
    {
        Ok(webview) => webview,
        Err(error) => fail(&error.to_string()),
    };
    let mut host = Host {
        _window: window,
        webview,
        shell: Shell::new(mods::pin()),
        proxy: event_loop.create_proxy(),
    };
    event_loop.run(move |event, _, control_flow| {
        *control_flow = ControlFlow::Wait;
        match event {
            TaoEvent::UserEvent(UserEvent::Ipc(command)) => {
                let steps = host.shell.handle(command);
                for step in steps {
                    host.apply(step, control_flow);
                }
            }
            TaoEvent::UserEvent(UserEvent::Emit(event)) => host.deliver(&event),
            TaoEvent::WindowEvent {
                event: WindowEvent::CloseRequested,
                ..
            } => {
                for step in host.shell.request_close() {
                    host.apply(step, control_flow);
                }
            }
            _ => {}
        }
    });
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn navigation_stays_on_the_launcher_page() {
        assert!(navigation_allowed(START_URL));
        assert!(navigation_allowed("co://localhost/launcher.js"));
        assert!(!navigation_allowed("https://example.com/"));
        assert!(!navigation_allowed("http://co.localhost.example/"));
    }
}
