/*
 * log data converter for DC power monitor
 *
 * Copyright (C) 2024 Hiroshi Kuwagata <kgt9221@gmail.com>
 */

///
/// プログラムのエントリポイント
///

use std::io::{Write, Error, ErrorKind};
use binrw::BinRead;

pub mod cmd_args;

/// データのエントリ（1エントリ文）を格納する構造体。
#[derive(BinRead, Debug)]
#[br(little)]
struct Entry {
    /// タイムスタンプ情報
    timestamp: u32,

    /// レジスタから読み出した電圧値
    voltage: i16,

    /// レジスタから読み出した電流値
    current: i16,
}

impl Entry {
    /// センサーから読み出した電圧値をVに変換するための係数
    const VOLTAGE_COEFFICIENT:f32 = 0.00125;

    /// センサーから読み出した電流値をmAに変換するための係数
    const CURRENT_COEFFICIENT:f32 = 0.1;

    ///
    /// 電圧値(V)の取得
    ///
    /// # 戻り値
    /// センサーから読み出した電圧値をVに変換した値を返す。
    ///
    fn get_voltage(&self) -> f32 {
        (self.voltage as f32) * Self::VOLTAGE_COEFFICIENT
    }

    ///
    /// 電流値(mA)の取得
    ///
    /// # 戻り値
    /// センサーから読み出した電流値をmAに変換した値を返す。
    ///
    fn get_current(&self) -> f32 {
        (self.current as f32) * Self::CURRENT_COEFFICIENT
    }
}

///
/// ヘッダの出力
///
fn write_header(output: &mut dyn Write) -> std::io::Result<()> {
    let bom: &[u8] =  b"\xef\xbb\xbf";
    let header: &[u8] = "\"timestamp\",\"voltage\",\"current\"\n".as_bytes();

    // Excelでの文字化けを避けるためにBOMを出力
    output.write_all(bom)?;

    // ヘッダを出力
    output.write_all(header)?;

    Ok(())
}

///
/// エラー変換関数
///
/// # 引数
/// `err` - 元のエラーオブジェクト
///
/// # 戻り値
/// std::io::Errorオブジェクト
///
fn io_error(err: &dyn ToString) -> Error {
    return Error::new(ErrorKind::Other, err.to_string());
}

///
/// プログラムの実行関数
///
fn run() -> std::io::Result<()> {
    let opts = cmd_args::parse();
    let ts_offset = opts.get_record_time().unwrap_or(0);
    let mut flag = false;
    let mut ts0 = 0;

    // 入力元を取得
    let mut input = opts.get_input_file()?;

    // 出力先を取得
    let mut output = opts.get_output_writer()?;

    // ヘッダを出力
    write_header(&mut output)?;

    // 1エントリづつ読みこみCSVに変換して出力
    loop {
        match Entry::read(&mut input) {
            Ok(entry) => {
                if !flag {
                    ts0 = entry.timestamp as i64;
                    flag = true;
                }

                let s = format!(
                    "{},{:.5},{:.1}\n",
                    ts_offset + ((entry.timestamp as i64) - ts0),
                    entry.get_voltage(),
                    entry.get_current()
                );

                output.write_all(s.as_bytes())?;
            }

            Err(err) => {
                if err.is_eof() {
                    break;
                } else {
                    return Err(io_error(&err));
                }
            }
        }
    }

    return Ok(());
}

///
/// プログラムのエントリポイント
///
fn main() {
    if let Err(err) = run() {
        eprintln!("Error: {}", err);
        std::process::exit(1);
    }
}
