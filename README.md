# DDNet-Block

A **block** gametype modification for **DDNet**, developed by **Nouaa** with the help of some wonderful contributors.

> ### Affiliation notice
>
> This project is **not affiliated with the Blockworlds community or the servers they operate.**
> I am no longer involved with that community in any capacity, and this repository is not
> maintained on their behalf.
>
> Blockworlds servers run an **older version** of this code.
>
> Versions released on or after **29 August 2026** are not licensed for use by that community.
> See [LICENSE-GAMEMODE](LICENSE-GAMEMODE.txt) for the exact terms.

## Table of Contents

- [About](#about)
- [Compatibility](#compatibility)
- [Requirements](#requirements)
- [Contributing](#contributing)
- [Contributors](#contributors)
- [Scope & Support](#scope--support)
- [License](#license)

## About

This repository contains a server-side modification implementing the **block** gametype, built on
top of **DDNet**.

Block is a PvP mode: instead of racing to finish a map, players use hook and
movement mechanics to knock each other out, with scoring and ranking built around kills and
survival. Everything specific to that mode, the gamemode logic, commands, scoring, and the
supporting server-side features lives in this repository.

## Compatibility

This is a **server-side only** modification and is fully compatible with **DDNet**.

- Any unmodified DDNet client can connect and play. No custom client, patch, or extra download is
  required from players.
- Standard DDNet client features work as expected.
- Nothing here requires players to install or trust anything beyond the official DDNet client.

## Requirements

- A working knowledge of building and running a basic **DDNet** server is required before using
  this repository.
- Refer to the [official DDNet repository](https://github.com/ddnet/ddnet) for base setup and build
  instructions.

## Contributing

- Feel free to use this code in accordance with the repository's license.
- Contributions are always welcome, if you'd like to improve the project, fix bugs, or add
  features, please open a Pull Request.
- By opening a Pull Request, you agree that your contribution is licensed under the terms in
  [LICENSE](LICENSE-GAMEMODE.txt), as those terms may be amended by the maintainer.
- Please do not use this code for malicious purposes.

## Contributors

A huge thanks to everyone who has contributed to this project:

| Contributor |
|---|
| melon |
| Anime.pdf |
| zhn |
| Sakido |
| qxdFox |
| DrToast |

## Scope & Support

> This repository only provides support for the **block** gametype as implemented here.

- Make sure you can build & run a basic DDNet server before using this repository, basic setup
  questions will not be answered here.
- No support is provided for the older version running on the Blockworlds servers. Questions about
  that deployment should be directed to whoever operates it.
- For help or questions about this repository, feel free to reach out on Discord: **nouaatw**

## License

This project is distributed under the terms in the [LICENSE](LICENSE-GAMEMODE.txt) file.

It is **source-available, not open source**: the license adds copyleft, source-availability, and
non-commercial terms on top of the original zlib license of the DDNet/Teeworlds base, and excludes
specific parties from any grant of permission. Read the file in full before using this code.

The license terms changed on **29 August 2026**. Versions released before that date remain under
the terms that applied at the time of their release.
