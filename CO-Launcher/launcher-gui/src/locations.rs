use std::fs;
use std::path::{Path, PathBuf};

use launcher_core::fsutil::{find_ci, write_atomic};
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(default, rename_all = "camelCase")]
pub struct Locations {
    pub game: String,
    pub mo2: String,
    pub vortex: String,
}

pub fn user_file() -> Option<PathBuf> {
    dirs::data_local_dir().map(|dir| {
        dir.join("CommonwealthOnlineLauncher")
            .join("launcher-locations.json")
    })
}

pub fn load(path: &Path) -> Result<Locations, String> {
    match fs::read(path) {
        Ok(bytes) if bytes.len() <= 65536 => serde_json::from_slice(&bytes)
            .map_err(|_| "The launcher locations file is invalid.".into()),
        Ok(_) => Err("The launcher locations file is too large.".into()),
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => Ok(Locations::default()),
        Err(error) => Err(format!("Could not read launcher locations: {error}")),
    }
}

pub fn save(path: &Path, value: &Locations) -> Result<(), String> {
    if !value.game.is_empty()
        && !find_ci(Path::new(&value.game), "Fallout4.exe").is_some_and(|path| path.is_file())
    {
        return Err("The game folder must contain Fallout4.exe.".into());
    }
    if !value.mo2.is_empty()
        && !find_ci(Path::new(&value.mo2), "ModOrganizer.ini").is_some_and(|path| path.is_file())
    {
        return Err("The Mod Organizer instance folder must contain ModOrganizer.ini.".into());
    }
    if !value.vortex.is_empty() && !Path::new(&value.vortex).is_dir() {
        return Err("The Vortex Fallout 4 staging folder does not exist.".into());
    }
    if !value.game.is_empty()
        && !value.mo2.is_empty()
        && launcher_core::modmgr::selected_instance(
            Path::new(&value.mo2),
            Some(Path::new(&value.game)),
        )
        .is_none()
    {
        return Err(
            "The Mod Organizer instance does not match the selected Fallout 4 folder.".into(),
        );
    }
    let bytes = serde_json::to_vec_pretty(value).map_err(|error| error.to_string())?;
    write_atomic(path, &bytes).map_err(|error| error.to_string())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn locations_validate_and_round_trip() {
        let temp = tempfile::tempdir().unwrap();
        let file = temp.path().join("data").join("launcher-locations.json");
        assert_eq!(load(&file).unwrap(), Locations::default());
        let game = temp.path().join("game");
        let mo2 = temp.path().join("mo2");
        let vortex = temp.path().join("vortex");
        for dir in [&game, &mo2, &vortex] {
            fs::create_dir(dir).unwrap();
        }
        fs::write(game.join("Fallout4.exe"), b"").unwrap();
        let ini = format!(
            "[General]\ngameName=Fallout 4\ngamePath=@ByteArray({})\n",
            game.to_string_lossy().replace('\\', "\\\\")
        );
        fs::write(mo2.join("ModOrganizer.ini"), ini).unwrap();
        let value = Locations {
            game: game.to_string_lossy().into_owned(),
            mo2: mo2.to_string_lossy().into_owned(),
            vortex: vortex.to_string_lossy().into_owned(),
        };
        save(&file, &value).unwrap();
        assert_eq!(load(&file).unwrap(), value);
        let updated = Locations {
            vortex: String::new(),
            ..value.clone()
        };
        save(&file, &updated).unwrap();
        assert_eq!(load(&file).unwrap(), updated);
        fs::write(&file, b"not valid json").unwrap();
        assert!(load(&file).is_err());
        save(&file, &updated).unwrap();
        assert_eq!(load(&file).unwrap(), updated);
        let mut invalid = value.clone();
        invalid.game = temp.path().join("missing").to_string_lossy().into_owned();
        assert!(save(&file, &invalid).is_err());
        assert_eq!(load(&file).unwrap(), updated);
        let other = temp.path().join("other");
        fs::create_dir(&other).unwrap();
        fs::write(other.join("Fallout4.exe"), b"").unwrap();
        invalid.game = other.to_string_lossy().into_owned();
        assert!(save(&file, &invalid).is_err());
        assert_eq!(load(&file).unwrap(), updated);
    }
}
