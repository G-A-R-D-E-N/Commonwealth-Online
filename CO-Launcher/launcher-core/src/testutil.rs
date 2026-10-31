use std::fs;
use std::path::Path;

use crate::runtime::Runtime;

const FILE_ALIGNMENT: u32 = 0x200;
const SECTION_ADDRESS: u32 = 0x1000;
const SUBDIRECTORY: u32 = 0x8000_0000;
const VERSION_INFO_OFFSET: u32 = 88;
const VERSION_INFO_LEN: u16 = 92;
const FIXED_FILE_INFO_LEN: u16 = 52;

pub(crate) fn write_fake_pe(path: &Path, version: (u16, u16, u16, u16)) {
    fs::write(path, fake_pe(version)).expect("write fake PE");
}

pub(crate) fn fake_game(dir: &Path, runtime: Runtime) {
    write_fake_pe(
        &dir.join("Fallout4.exe"),
        version_tuple(runtime.game_version()),
    );
}

fn version_tuple(text: &str) -> (u16, u16, u16, u16) {
    let parts: Vec<u16> = text
        .split('.')
        .map(|part| part.parse().expect("numeric version part"))
        .collect();
    (parts[0], parts[1], parts[2], 0)
}

fn fake_pe(version: (u16, u16, u16, u16)) -> Vec<u8> {
    let resources = resource_section(version);
    let len = resources.len() as u32;
    let mut image = b"MZ".to_vec();
    image.resize(0x3C, 0);
    image.extend(0x40u32.to_le_bytes());
    image.extend(b"PE\0\0");
    image.extend(0x8664u16.to_le_bytes());
    image.extend(1u16.to_le_bytes());
    image.extend([0u8; 12]);
    image.extend(240u16.to_le_bytes());
    image.extend(0x0022u16.to_le_bytes());
    image.extend(optional_header(len));
    image.extend(section_header(len));
    image.resize(FILE_ALIGNMENT as usize, 0);
    image.extend(resources);
    image.resize(2 * FILE_ALIGNMENT as usize, 0);
    image
}

fn optional_header(resource_len: u32) -> Vec<u8> {
    let mut header = Vec::with_capacity(240);
    header.extend(0x20Bu16.to_le_bytes());
    header.extend([0u8; 22]);
    header.extend(0x1_4000_0000u64.to_le_bytes());
    header.extend(SECTION_ADDRESS.to_le_bytes());
    header.extend(FILE_ALIGNMENT.to_le_bytes());
    header.extend([0u8; 12]);
    header.extend(0u32.to_le_bytes());
    header.extend(0x2000u32.to_le_bytes());
    header.extend(FILE_ALIGNMENT.to_le_bytes());
    header.extend([0u8; 8]);
    header.extend([0u8; 32]);
    header.extend(0u32.to_le_bytes());
    header.extend(16u32.to_le_bytes());
    let mut directories = [0u8; 128];
    directories[16..20].copy_from_slice(&SECTION_ADDRESS.to_le_bytes());
    directories[20..24].copy_from_slice(&resource_len.to_le_bytes());
    header.extend(directories);
    header
}

fn section_header(resource_len: u32) -> Vec<u8> {
    let mut header = Vec::with_capacity(40);
    header.extend(b".rsrc\0\0\0");
    header.extend(resource_len.to_le_bytes());
    header.extend(SECTION_ADDRESS.to_le_bytes());
    header.extend(FILE_ALIGNMENT.to_le_bytes());
    header.extend(FILE_ALIGNMENT.to_le_bytes());
    header.extend([0u8; 12]);
    header.extend(0x4000_0040u32.to_le_bytes());
    header
}

fn resource_section(version: (u16, u16, u16, u16)) -> Vec<u8> {
    let info = version_info(version);
    let mut tree = Vec::new();
    tree.extend(directory_table(16, SUBDIRECTORY | 24));
    tree.extend(directory_table(1, SUBDIRECTORY | 48));
    tree.extend(directory_table(0x0409, 72));
    tree.extend((SECTION_ADDRESS + VERSION_INFO_OFFSET).to_le_bytes());
    tree.extend((info.len() as u32).to_le_bytes());
    tree.extend([0u8; 8]);
    tree.extend(info);
    tree
}

fn directory_table(id: u32, target: u32) -> Vec<u8> {
    let mut table = vec![0u8; 12];
    table.extend(0u16.to_le_bytes());
    table.extend(1u16.to_le_bytes());
    table.extend(id.to_le_bytes());
    table.extend(target.to_le_bytes());
    table
}

fn version_info(version: (u16, u16, u16, u16)) -> Vec<u8> {
    let (major, minor, patch, build) = version;
    let ms = (u32::from(major) << 16) | u32::from(minor);
    let ls = (u32::from(patch) << 16) | u32::from(build);
    let mut blob = Vec::with_capacity(VERSION_INFO_LEN as usize);
    blob.extend(VERSION_INFO_LEN.to_le_bytes());
    blob.extend(FIXED_FILE_INFO_LEN.to_le_bytes());
    blob.extend(0u16.to_le_bytes());
    for unit in "VS_VERSION_INFO".encode_utf16().chain(std::iter::once(0)) {
        blob.extend(unit.to_le_bytes());
    }
    blob.extend([0u8; 2]);
    blob.extend(0xFEEF_04BDu32.to_le_bytes());
    blob.extend(0x0001_0000u32.to_le_bytes());
    blob.extend(ms.to_le_bytes());
    blob.extend(ls.to_le_bytes());
    blob.extend(ms.to_le_bytes());
    blob.extend(ls.to_le_bytes());
    blob.extend([0u8; 28]);
    blob
}
