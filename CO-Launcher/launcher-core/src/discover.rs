use std::fs;
use std::path::{Path, PathBuf};

use crate::error::{Error, Result};
use crate::fsutil::{find_ci, resolve_ci};

pub const FALLOUT4_APP_ID: &str = "377160";

const GAME_EXE: &str = "Fallout4.exe";
const DEFAULT_INSTALL_DIR: &str = "Fallout 4";
const MAX_DEPTH: usize = 32;

#[derive(Debug, Clone, Default)]
pub struct Env {
    pub home: Option<PathBuf>,
    pub registry_steam_paths: Vec<PathBuf>,
}

impl Env {
    pub fn detect() -> Env {
        Env {
            home: dirs::home_dir(),
            registry_steam_paths: registry_steam_paths(),
        }
    }
}

#[cfg(windows)]
fn registry_steam_paths() -> Vec<PathBuf> {
    use winreg::enums::{HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE};
    use winreg::RegKey;
    [
        (HKEY_CURRENT_USER, r"Software\Valve\Steam", "SteamPath"),
        (
            HKEY_LOCAL_MACHINE,
            r"SOFTWARE\WOW6432Node\Valve\Steam",
            "InstallPath",
        ),
    ]
    .into_iter()
    .filter_map(|(hive, subkey, name)| {
        RegKey::predef(hive)
            .open_subkey(subkey)
            .ok()?
            .get_value::<String, _>(name)
            .ok()
    })
    .map(PathBuf::from)
    .collect()
}

#[cfg(not(windows))]
fn registry_steam_paths() -> Vec<PathBuf> {
    Vec::new()
}

pub fn parse_library_folders(vdf: &str) -> Vec<PathBuf> {
    let tree = parse(vdf);
    section(&tree, "libraryfolders")
        .iter()
        .filter(|(key, _)| !key.is_empty() && key.bytes().all(|b| b.is_ascii_digit()))
        .filter_map(|(_, node)| match node {
            Node::Text(path) => Some(path.as_str()),
            Node::Block(inner) => text(inner, "path"),
        })
        .filter(|path| !path.is_empty())
        .map(PathBuf::from)
        .collect()
}

pub fn steam_roots(env: &Env) -> Vec<PathBuf> {
    let mut roots = env.registry_steam_paths.clone();
    if cfg!(all(windows, not(test))) {
        roots.push(PathBuf::from(r"C:\Program Files (x86)\Steam"));
    }
    if let Some(home) = &env.home {
        roots.push(home.join(".steam").join("steam"));
        roots.push(home.join(".local/share/Steam"));
        roots.push(home.join(".var/app/com.valvesoftware.Steam/.local/share/Steam"));
    }
    unique(&roots)
}

pub fn find_game(override_path: Option<&Path>, env: &Env) -> Result<PathBuf> {
    if let Some(dir) = override_path {
        if find_ci(dir, GAME_EXE).is_some() {
            return Ok(dir.to_path_buf());
        }
        return Err(not_found(&[dir.to_path_buf()]));
    }
    let candidates = steam_candidates(env);
    candidates
        .iter()
        .find(|dir| find_ci(dir, GAME_EXE).is_some())
        .cloned()
        .ok_or_else(|| not_found(&candidates))
}

fn steam_candidates(env: &Env) -> Vec<PathBuf> {
    steam_roots(env)
        .iter()
        .flat_map(|root| libraries(root))
        .flat_map(|library| game_dirs(&library))
        .collect()
}

fn libraries(root: &Path) -> Vec<PathBuf> {
    let vdf = resolve_ci(root, &["steamapps", "libraryfolders.vdf"]);
    let text = fs::read_to_string(vdf).unwrap_or_default();
    let mut libraries = vec![root.to_path_buf()];
    libraries.extend(parse_library_folders(&text));
    unique(&libraries)
}

fn game_dirs(library: &Path) -> Vec<PathBuf> {
    let steamapps = resolve_ci(library, &["steamapps"]);
    let common = steamapps.join("common");
    let named = installdir(&steamapps).unwrap_or_else(|| DEFAULT_INSTALL_DIR.to_string());
    let mut dirs = vec![resolve_ci(&common, &[named.as_str()])];
    if named != DEFAULT_INSTALL_DIR && find_ci(&common, &named).is_none() {
        dirs.push(resolve_ci(&common, &[DEFAULT_INSTALL_DIR]));
    }
    dirs
}

fn installdir(steamapps: &Path) -> Option<String> {
    let manifest = format!("appmanifest_{FALLOUT4_APP_ID}.acf");
    let content = fs::read_to_string(steamapps.join(manifest)).ok()?;
    let tree = parse(&content);
    text(section(&tree, "AppState"), "installdir")
        .filter(|name| !name.is_empty())
        .map(str::to_string)
}

fn not_found(paths: &[PathBuf]) -> Error {
    Error::GameNotFound {
        tried: unique(paths)
            .iter()
            .map(|p| p.display().to_string())
            .collect(),
    }
}

fn unique(paths: &[PathBuf]) -> Vec<PathBuf> {
    let mut out: Vec<PathBuf> = Vec::new();
    for path in paths {
        if !path.as_os_str().is_empty() && !out.contains(path) {
            out.push(path.clone());
        }
    }
    out
}

enum Token {
    Text(String),
    Open,
    Close,
}

enum Node {
    Text(String),
    Block(Vec<(String, Node)>),
}

type Pairs = Vec<(String, Node)>;

fn is_space(c: char) -> bool {
    c.is_whitespace() || c == '\u{feff}'
}

fn tokenize(text: &str) -> Vec<Token> {
    let mut tokens = Vec::new();
    let mut chars = text.chars().peekable();
    while let Some(c) = chars.next() {
        match c {
            '{' => tokens.push(Token::Open),
            '}' => tokens.push(Token::Close),
            '"' => {
                let mut value = String::new();
                while let Some(c) = chars.next() {
                    match c {
                        '"' => break,
                        '\\' => match chars.next() {
                            Some('\\') => value.push('\\'),
                            Some('"') => value.push('"'),
                            Some('n') => value.push('\n'),
                            Some('t') => value.push('\t'),
                            Some(other) => {
                                value.push('\\');
                                value.push(other);
                            }
                            None => value.push('\\'),
                        },
                        _ => value.push(c),
                    }
                }
                tokens.push(Token::Text(value));
            }
            '/' if chars.peek() == Some(&'/') => {
                for c in chars.by_ref() {
                    if c == '\n' {
                        break;
                    }
                }
            }
            c if is_space(c) => {}
            _ => {
                let mut value = String::from(c);
                while let Some(&next) = chars.peek() {
                    if is_space(next) || matches!(next, '"' | '{' | '}') {
                        break;
                    }
                    value.push(next);
                    chars.next();
                }
                tokens.push(Token::Text(value));
            }
        }
    }
    tokens
}

fn parse(text: &str) -> Pairs {
    let mut stack: Vec<(String, Pairs)> = vec![(String::new(), Vec::new())];
    let mut key = None;
    let mut skipped = 0;
    for token in tokenize(text) {
        let pending = key.take();
        match token {
            Token::Open if skipped > 0 || stack.len() > MAX_DEPTH => skipped += 1,
            Token::Close if skipped > 0 => skipped -= 1,
            _ if skipped > 0 => {}
            Token::Text(value) => match pending {
                Some(k) => add(&mut stack, k, Node::Text(value)),
                None => key = Some(value),
            },
            Token::Open => stack.push((pending.unwrap_or_default(), Vec::new())),
            Token::Close => close(&mut stack),
        }
    }
    while stack.len() > 1 {
        close(&mut stack);
    }
    stack.remove(0).1
}

fn close(stack: &mut Vec<(String, Pairs)>) {
    if stack.len() > 1 {
        let (key, pairs) = stack.pop().unwrap_or_default();
        add(stack, key, Node::Block(pairs));
    }
}

fn add(stack: &mut [(String, Pairs)], key: String, node: Node) {
    if let Some((_, pairs)) = stack.last_mut() {
        pairs.push((key, node));
    }
}

fn get<'a>(pairs: &'a [(String, Node)], key: &str) -> Option<&'a Node> {
    pairs
        .iter()
        .find(|(k, _)| k.eq_ignore_ascii_case(key))
        .map(|(_, node)| node)
}

fn section<'a>(pairs: &'a [(String, Node)], name: &str) -> &'a [(String, Node)] {
    match get(pairs, name) {
        Some(Node::Block(inner)) => inner.as_slice(),
        _ => pairs,
    }
}

fn text<'a>(pairs: &'a [(String, Node)], key: &str) -> Option<&'a str> {
    match get(pairs, key) {
        Some(Node::Text(value)) => Some(value.as_str()),
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn install(dir: &Path, exe: &str) {
        fs::create_dir_all(dir).unwrap();
        fs::write(dir.join(exe), b"").unwrap();
    }

    fn write(path: &Path, text: &str) {
        fs::create_dir_all(path.parent().unwrap()).unwrap();
        fs::write(path, text).unwrap();
    }

    fn vdf_path(path: &Path) -> String {
        path.display().to_string().replace('\\', "\\\\")
    }

    fn env_home(home: &Path) -> Env {
        Env {
            home: Some(home.to_path_buf()),
            registry_steam_paths: Vec::new(),
        }
    }

    fn game_in(library: &Path, dir: &str) -> PathBuf {
        library.join("steamapps").join("common").join(dir)
    }

    fn tried_of(result: Result<PathBuf>) -> Vec<String> {
        match result {
            Err(Error::GameNotFound { tried }) => tried,
            other => panic!("expected GameNotFound, got {other:?}"),
        }
    }

    #[test]
    fn parses_new_format_library_paths() {
        let vdf = r#""libraryfolders"
{
	"0"
	{
		"path"		"C:\\Program Files (x86)\\Steam"
		"apps"
		{
			"228980"		"381331115"
		}
	}
	"1"
	{
		"path"		"D:\\SteamLibrary"
	}
}"#;
        assert_eq!(
            parse_library_folders(vdf),
            vec![
                PathBuf::from(r"C:\Program Files (x86)\Steam"),
                PathBuf::from(r"D:\SteamLibrary"),
            ]
        );
    }

    #[test]
    fn parses_old_format_and_ignores_other_keys() {
        let vdf = r#""LibraryFolders"
{
	"TimeNextStatsReport"		"1700000000"
	"ContentStatsID"		"-123"
	"1"		"E:\\Games\\Steam"
}"#;
        assert_eq!(
            parse_library_folders(vdf),
            vec![PathBuf::from(r"E:\Games\Steam")]
        );
    }

    #[test]
    fn undoes_escapes_and_tolerates_comments_and_whitespace() {
        let vdf = r#"// header comment
"libraryfolders"{
	"0" { "path" "D:\\Games\\\"Q\"" }  // trailing
	"1"	"E:\\X	Y\n"
}
"#;
        assert_eq!(
            parse_library_folders(vdf),
            vec![
                PathBuf::from("D:\\Games\\\"Q\""),
                PathBuf::from("E:\\X\tY\n")
            ]
        );
    }

    #[test]
    fn malformed_vdf_returns_what_was_parsed() {
        let vdf = r#""libraryfolders" { "1" { "path" "E:\\Good" } } } } "2" {"#;
        assert_eq!(parse_library_folders(vdf), vec![PathBuf::from(r"E:\Good")]);
        assert!(parse_library_folders("\"1\" \"unterminated \\").len() <= 1);
        assert!(parse_library_folders(&"\"a\" {".repeat(100_000)).is_empty());
        assert!(parse_library_folders(&"}".repeat(100_000)).is_empty());
        assert!(parse_library_folders("").is_empty());
    }

    #[test]
    fn steam_roots_keep_order_and_drop_duplicates() {
        let home = PathBuf::from("/home/deck");
        let registry = PathBuf::from("/opt/steam-registry");
        let env = Env {
            home: Some(home.clone()),
            registry_steam_paths: vec![home.join(".steam").join("steam"), registry.clone()],
        };
        assert_eq!(
            steam_roots(&env),
            vec![
                home.join(".steam").join("steam"),
                registry,
                home.join(".local/share/Steam"),
                home.join(".var/app/com.valvesoftware.Steam/.local/share/Steam"),
            ]
        );
    }

    #[test]
    fn finds_game_under_dot_steam_steam() {
        let tmp = tempfile::tempdir().unwrap();
        let root = tmp.path().join(".steam").join("steam");
        let game = game_in(&root, DEFAULT_INSTALL_DIR);
        install(&game, GAME_EXE);
        assert_eq!(find_game(None, &env_home(tmp.path())).unwrap(), game);
    }

    #[test]
    fn finds_game_under_local_share_steam() {
        let tmp = tempfile::tempdir().unwrap();
        let game = game_in(&tmp.path().join(".local/share/Steam"), DEFAULT_INSTALL_DIR);
        install(&game, GAME_EXE);
        assert_eq!(find_game(None, &env_home(tmp.path())).unwrap(), game);
    }

    #[test]
    fn finds_game_in_flatpak_steam() {
        let tmp = tempfile::tempdir().unwrap();
        let root = tmp
            .path()
            .join(".var/app/com.valvesoftware.Steam/.local/share/Steam");
        let game = game_in(&root, DEFAULT_INSTALL_DIR);
        install(&game, GAME_EXE);
        assert_eq!(find_game(None, &env_home(tmp.path())).unwrap(), game);
    }

    #[test]
    fn finds_game_under_registry_path() {
        let tmp = tempfile::tempdir().unwrap();
        let root = tmp.path().join("Steam");
        let game = game_in(&root, DEFAULT_INSTALL_DIR);
        install(&game, GAME_EXE);
        let env = Env {
            home: None,
            registry_steam_paths: vec![root],
        };
        assert_eq!(find_game(None, &env).unwrap(), game);
    }

    #[test]
    fn finds_game_in_second_library_new_format() {
        let tmp = tempfile::tempdir().unwrap();
        let root = tmp.path().join("steam");
        let lib = tmp.path().join("games");
        write(
            &root.join("steamapps").join("libraryfolders.vdf"),
            &format!(
                r#""libraryfolders"
{{
	"0"
	{{
		"path"		"{}"
		"apps"
		{{
			"228980"		"1"
		}}
	}}
	"1"
	{{
		"path"		"{}"
	}}
}}"#,
                vdf_path(&root),
                vdf_path(&lib)
            ),
        );
        let game = game_in(&lib, DEFAULT_INSTALL_DIR);
        install(&game, GAME_EXE);
        let env = Env {
            home: None,
            registry_steam_paths: vec![root],
        };
        assert_eq!(find_game(None, &env).unwrap(), game);
    }

    #[test]
    fn finds_game_in_library_old_format() {
        let tmp = tempfile::tempdir().unwrap();
        let root = tmp.path().join("steam");
        let lib = tmp.path().join("games");
        write(
            &root.join("steamapps").join("libraryfolders.vdf"),
            &format!(
                "\"LibraryFolders\"\n{{\n\t\"TimeNextStatsReport\"\t\t\"1\"\n\t\"1\"\t\t\"{}\"\n}}\n",
                vdf_path(&lib)
            ),
        );
        let game = game_in(&lib, DEFAULT_INSTALL_DIR);
        install(&game, GAME_EXE);
        let env = Env {
            home: None,
            registry_steam_paths: vec![root],
        };
        assert_eq!(find_game(None, &env).unwrap(), game);
    }

    #[test]
    fn uses_custom_installdir_from_manifest() {
        let tmp = tempfile::tempdir().unwrap();
        let root = tmp.path().join(".steam").join("steam");
        write(
            &root.join("steamapps").join("appmanifest_377160.acf"),
            "\"AppState\"\n{\n\t\"appid\"\t\t\"377160\"\n\t\"installdir\"\t\t\"Fallout 4 Custom\"\n}\n",
        );
        let game = game_in(&root, "Fallout 4 Custom");
        install(&game, GAME_EXE);
        assert_eq!(find_game(None, &env_home(tmp.path())).unwrap(), game);
    }

    #[test]
    fn manifest_missing_uses_default_dir() {
        let tmp = tempfile::tempdir().unwrap();
        let root = tmp.path().join(".steam").join("steam");
        let game = game_in(&root, DEFAULT_INSTALL_DIR);
        install(&game, GAME_EXE);
        assert_eq!(find_game(None, &env_home(tmp.path())).unwrap(), game);
    }

    #[test]
    fn named_installdir_missing_falls_back_to_default_dir() {
        let tmp = tempfile::tempdir().unwrap();
        let root = tmp.path().join(".steam").join("steam");
        write(
            &root.join("steamapps").join("appmanifest_377160.acf"),
            "\"AppState\" { \"installdir\" \"Elsewhere\" }",
        );
        let game = game_in(&root, DEFAULT_INSTALL_DIR);
        install(&game, GAME_EXE);
        assert_eq!(find_game(None, &env_home(tmp.path())).unwrap(), game);
    }

    #[test]
    fn override_path_with_exe_wins() {
        let tmp = tempfile::tempdir().unwrap();
        let dir = tmp.path().join("Fallout 4");
        install(&dir, GAME_EXE);
        assert_eq!(
            find_game(Some(dir.as_path()), &Env::default()).unwrap(),
            dir
        );
    }

    #[test]
    fn override_path_without_exe_is_not_found() {
        let tmp = tempfile::tempdir().unwrap();
        let dir = tmp.path().join("empty");
        fs::create_dir_all(&dir).unwrap();
        let tried = tried_of(find_game(Some(dir.as_path()), &Env::default()));
        assert_eq!(tried, vec![dir.display().to_string()]);
    }

    #[test]
    fn lowercase_exe_is_found() {
        let tmp = tempfile::tempdir().unwrap();
        let game = game_in(&tmp.path().join(".steam/steam"), DEFAULT_INSTALL_DIR);
        install(&game, "fallout4.exe");
        assert_eq!(find_game(None, &env_home(tmp.path())).unwrap(), game);
    }

    #[test]
    fn nothing_found_lists_every_candidate() {
        let tmp = tempfile::tempdir().unwrap();
        let home = tmp.path().join("home");
        let reg = tmp.path().join("reg");
        let env = Env {
            home: Some(home.clone()),
            registry_steam_paths: vec![reg.clone()],
        };
        let roots = [
            reg,
            home.join(".steam").join("steam"),
            home.join(".local/share/Steam"),
            home.join(".var/app/com.valvesoftware.Steam/.local/share/Steam"),
        ];
        let expected: Vec<PathBuf> = roots
            .iter()
            .map(|r| game_in(r, DEFAULT_INSTALL_DIR))
            .collect();
        let tried = tried_of(find_game(None, &env));
        assert_eq!(
            tried.iter().map(PathBuf::from).collect::<Vec<_>>(),
            expected
        );
    }

    #[test]
    fn malformed_library_vdf_still_finds_listed_library() {
        let tmp = tempfile::tempdir().unwrap();
        let root = tmp.path().join("steam");
        let lib = tmp.path().join("games");
        write(
            &root.join("steamapps").join("libraryfolders.vdf"),
            &format!(
                "\"libraryfolders\" {{ \"1\" {{ \"path\" \"{}\" }} ][ \"\\",
                vdf_path(&lib)
            ),
        );
        let game = game_in(&lib, DEFAULT_INSTALL_DIR);
        install(&game, GAME_EXE);
        let env = Env {
            home: None,
            registry_steam_paths: vec![root],
        };
        assert_eq!(find_game(None, &env).unwrap(), game);
    }

    #[test]
    fn lists_the_install_in_every_steam_library() {
        let tmp = tempfile::tempdir().unwrap();
        let root = tmp.path().join("steam");
        let lib = tmp.path().join("games");
        write(
            &root.join("steamapps").join("libraryfolders.vdf"),
            &format!(
                "\"libraryfolders\" {{ \"0\" {{ \"path\" \"{}\" }} \"1\" {{ \"path\" \"{}\" }} }}",
                vdf_path(&root),
                vdf_path(&lib)
            ),
        );
        let first = game_in(&root, DEFAULT_INSTALL_DIR);
        let second = game_in(&lib, DEFAULT_INSTALL_DIR);
        install(&first, GAME_EXE);
        install(&second, GAME_EXE);
        let env = Env {
            home: None,
            registry_steam_paths: vec![root],
        };
        let found: Vec<PathBuf> = steam_candidates(&env)
            .into_iter()
            .filter(|dir| find_ci(dir, GAME_EXE).is_some())
            .collect();
        assert_eq!(found, vec![first, second]);
    }
}
