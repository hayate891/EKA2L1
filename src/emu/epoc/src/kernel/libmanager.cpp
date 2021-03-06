/*
 * Copyright (c) 2018 EKA2L1 Team.
 * 
 * This file is part of EKA2L1 project 
 * (see bentokun.github.com/EKA2L1).
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include <common/algorithm.h>
#include <common/cvt.h>
#include <common/log.h>
#include <common/path.h>
#include <common/random.h>

#include <epoc/kernel/libmanager.h>
#include <epoc/reg.h>

#include <common/configure.h>

#include <manager/manager.h>
#ifdef ENABLE_SCRIPTING
#include <manager/script_manager.h>
#endif

#include <manager/config.h>

#include <epoc/loader/e32img.h>
#include <epoc/loader/romimage.h>
#include <epoc/vfs.h>

#include <common/configure.h>
#include <epoc/epoc.h>
#include <epoc/kernel.h>
#include <epoc/kernel/codeseg.h>

#include <arm/arm_analyser.h>
#include <cctype>

namespace eka2l1 {
    namespace hle {
        // Write simple relocation
        bool write(uint32_t *data, uint32_t sym) {
            *data = sym;
            return true;
        }

        bool relocate(uint32_t *dest_ptr, loader::relocation_type type, uint32_t code_delta, uint32_t data_delta) {
            if (type == loader::relocation_type::reserved) {
                return true;
            }

            // What is in it ?? :))
            uint32_t reloc_offset = *dest_ptr;

            switch (type) {
            case loader::relocation_type::text:
                write(dest_ptr, reloc_offset + code_delta);
                break;

            case loader::relocation_type::data:
                // This is relocation for dynamic data.
                write(dest_ptr, reloc_offset + data_delta);
                break;

            case loader::relocation_type::inffered:
            default:
                LOG_WARN("Relocation not properly handle: {}", (int)type);
                break;
            }

            return true;
        }

        // Given relocation entries, relocate the code and data
        bool relocate(std::vector<loader::e32_reloc_entry> entries,
            uint8_t *dest_addr,
            uint32_t code_delta,
            uint32_t data_delta) {
            for (uint32_t i = 0; i < entries.size(); i++) {
                auto entry = entries[i];

                for (auto &rel_info : entry.rels_info) {
                    // Get the lower 12 bit for virtual_address
                    uint32_t virtual_addr = entry.base + (rel_info & 0x0FFF);
                    uint8_t *dest_ptr = virtual_addr + dest_addr;

                    loader::relocation_type rel_type = (loader::relocation_type)(rel_info & 0xF000);

                    // Compare with petran in case there are problems
                    // LOG_TRACE("{:x}", virtual_addr);

                    if (!relocate(reinterpret_cast<uint32_t *>(dest_ptr), rel_type, code_delta, data_delta)) {
                        LOG_WARN("Relocate fail at page: {}", i);
                    }
                }
            }

            return true;
        }

        std::string get_real_dll_name(std::string dll_name) {
            size_t dll_name_end_pos = dll_name.find_first_of("{");

            if (FOUND_STR(dll_name_end_pos)) {
                dll_name = dll_name.substr(0, dll_name_end_pos);
            } else {
                dll_name_end_pos = dll_name.find_last_of(".");

                if (FOUND_STR(dll_name_end_pos)) {
                    dll_name = dll_name.substr(0, dll_name_end_pos);
                }
            }

            return dll_name + ".dll";
        }

        bool pe_fix_up_iat(memory_system *mem, hle::lib_manager &mngr, loader::e32img &me,
            loader::e32img_import_block &import_block, loader::e32img_iat &iat, uint32_t &crr_idx,
            codeseg_ptr &parent_codeseg) {
            const std::string dll_name8 = get_real_dll_name(import_block.dll_name);
            const std::u16string dll_name = common::utf8_to_ucs2(dll_name8);

            codeseg_ptr cs = mngr.load(dll_name);

            if (!cs) {
                LOG_TRACE("Can't found {}", dll_name8);
                return false;
            }

            // Add dependency for the codeseg
            assert(parent_codeseg->add_dependency(cs));

            uint32_t *imdir = &(import_block.ordinals[0]);

            for (uint32_t i = crr_idx, j = 0;
                 i < crr_idx + import_block.ordinals.size() && j < import_block.ordinals.size();
                 i++, j++) {
                uint32_t iat_off = me.header.code_offset + me.header.code_size;
                *reinterpret_cast<std::uint32_t *>(me.data[iat_off + i * 4]) = cs->lookup(import_block.ordinals[j]);
            }

            crr_idx += static_cast<uint32_t>(import_block.ordinals.size());
            return true;
        }

        bool elf_fix_up_import_dir(memory_system *mem, hle::lib_manager &mngr, loader::e32img &me,
            loader::e32img_import_block &import_block, codeseg_ptr &parent_cs) {
            LOG_INFO("Fixup for: {}", import_block.dll_name);

            const std::string dll_name8 = get_real_dll_name(import_block.dll_name);
            const std::u16string dll_name = common::utf8_to_ucs2(dll_name8);

            codeseg_ptr cs = mngr.load(dll_name);

            if (!cs) {
                LOG_TRACE("Can't found {}", dll_name8);
                return false;
            }

            // Add that codeseg as our dependency
            assert(parent_cs->add_dependency(cs));

            std::uint32_t *imdir = &(import_block.ordinals[0]);

            for (uint32_t i = 0; i < import_block.ordinals.size(); i++) {
                uint32_t off = imdir[i];
                uint32_t *code_ptr = ptr<uint32_t>(me.rt_code_addr + off).get(mem);

                uint32_t import_inf = *code_ptr;
                uint32_t ord = import_inf & 0xffff;
                uint32_t adj = import_inf >> 16;

                uint32_t export_addr;
                uint32_t val = 0;

                if (ord > 0) {
                    export_addr = cs->lookup(ord);
                    assert(export_addr != 0);

                    // The export address provided is already added with relative code/data
                    // delta, so add this directly to the adjustment address
                    val = export_addr + adj;
                }

                // LOG_TRACE("Writing 0x{:x} to 0x{:x}", val, me.header.code_base + off);
                write(code_ptr, val);
            }

            return true;
        }

        codeseg_ptr import_e32img(loader::e32img *img, memory_system *mem, kernel_system *kern, hle::lib_manager &mngr, const std::u16string &path = u"") {
            std::uint32_t data_seg_size = img->header.data_size + img->header.bss_size;
            kernel::codeseg_create_info info;

            info.full_path = path;
            info.uids[0] = static_cast<std::uint32_t>(img->header.uid1);
            info.uids[1] = img->header.uid2;
            info.uids[2] = img->header.uid3;
            info.code_base = img->header.code_base;
            info.data_base = img->header.data_base;
            info.code_size = img->header.code_size;
            info.data_size = img->header.data_size;
            info.bss_size = img->header.bss_size;
            info.entry_point = img->header.entry_point;
            info.export_table = img->ed.syms;

            if (img->has_extended_header) {
                info.sinfo.caps_u[0] = img->header_extended.info.cap1;
                info.sinfo.caps_u[1] = img->header_extended.info.cap2;
                info.sinfo.vendor_id = img->header_extended.info.vendor_id;
                info.sinfo.secure_id = img->header_extended.info.secure_id;

                if (img->has_extended_header && (img->header_extended.exception_des & 1)) {
                    info.exception_descriptor = img->header_extended.exception_des - 1;
                } else {
                    info.exception_descriptor = 0;
                }
            }

            codeseg_ptr cs = kern->create<kernel::codeseg>("codeseg", info);
            mngr.register_exports(
                common::ucs2_to_utf8(eka2l1::replace_extension(eka2l1::filename(path), u"")),
                cs->get_export_table());

            uint32_t rtcode_addr = cs->get_code_run_addr();
            uint32_t rtdata_addr = cs->get_data_run_addr();

            LOG_INFO("Runtime code: 0x{:x}", rtcode_addr);

            img->rt_code_addr = rtcode_addr;
            img->rt_data_addr = rtdata_addr;

            // Ram code address is the run time address of the code
            uint32_t code_delta = rtcode_addr - img->header.code_base;
            uint32_t data_delta = rtdata_addr - img->header.data_base;

            relocate(img->code_reloc_section.entries, reinterpret_cast<uint8_t *>(img->data.data() + img->header.code_offset), code_delta, data_delta);

            if (img->header.data_size)
                relocate(img->data_reloc_section.entries, reinterpret_cast<uint8_t *>(img->data.data() + img->header.data_offset), code_delta, data_delta);

            if (img->epoc_ver == epocver::epoc6) {
                uint32_t track = 0;

                for (auto &ib : img->import_section.imports) {
                    pe_fix_up_iat(mem, mngr, *img, ib, img->iat, track, cs);
                }
            }

            memcpy(ptr<void>(rtcode_addr).get(mem), img->data.data() + img->header.code_offset, img->header.code_size);
            std::uint8_t *dt_ptr = ptr<std::uint8_t>(rtdata_addr).get(mem);

            if (img->header.data_size) {
                // If there is initialized data, copy that
                memcpy(dt_ptr, img->data.data() + img->header.data_offset,
                    img->header.data_size);
            }

            if (static_cast<int>(img->epoc_ver) >= static_cast<int>(epocver::epoc93)) {
                for (auto &ib : img->import_section.imports) {
                    elf_fix_up_import_dir(mem, mngr, *img, ib, cs);
                }
            }

            LOG_INFO("Load e32img success");
            return cs;
        }

        void lib_manager::init(system *syss, kernel_system *kerns, io_system *ios, memory_system *mems, epocver ver) {
            sys = syss;
            io = ios;
            mem = mems;
            kern = kerns;

            // TODO (pent0): Implement external id loading

            hle::symbols sb;
            std::string lib_name;

#define LIB(x) lib_name = #x;
#define EXPORT(x, y)   \
    sb.push_back(x);
#define ENDLIB()                                                                        \
    lib_symbols.emplace(lib_name, sb);                                                  \
    sb.clear();

            if (ver == epocver::epoc6) {
                //  #include <hle/epoc6_n.def>
            } else {
                // #include <hle/epoc9_n.def>
            }

#undef LIB
#undef EXPORT
#undef ENLIB

            if (ver == epocver::epoc94) {
                epoc::register_epocv94(*this);
            } else if (ver == epocver::epoc93) {
                epoc::register_epocv93(*this);
            }

            stub = kern->create<kernel::chunk>(kern->get_memory_system(),
                kern->crr_process(), "", 0, 0x5000, 0x5000, prot::read_write, kernel::chunk_type::disconnected,
                kernel::chunk_access::code, kernel::chunk_attrib::none, false);

            custom_stub = kern->create<kernel::chunk>(kern->get_memory_system(), kern->crr_process(),
                "", 0, 0x5000, 0x5000, prot::read_write, kernel::chunk_type::disconnected,
                kernel::chunk_access::code, kernel::chunk_attrib::none, false);
        }

        codeseg_ptr lib_manager::load_as_e32img(loader::e32img &img, const std::u16string &path) {
            if (auto seg = kern->pull_codeseg_by_uids(static_cast<std::uint32_t>(img.header.uid1),
                    img.header.uid2, img.header.uid3)) {
                return seg;
            }

            auto analyser = arm::make_analyser(arm::arm_disassembler_backend::capstone,
                [&](const vaddress offset) {
                    return *reinterpret_cast<std::uint32_t *>(&img.data[img.header.code_offset + offset - img.header.code_base]);
                });

            std::unordered_map<vaddress, arm::arm_function> funcs;
            analyser->analyse(funcs, img.header.entry_point + img.header.code_base, img.header.code_base + img.header.code_size);

            LOG_TRACE("Analyzing {}, found {} functions", common::ucs2_to_utf8(path), funcs.size());

            return import_e32img(&img, mem, kern, *this, path);
        }

        codeseg_ptr lib_manager::load_as_romimg(loader::romimg &romimg, const std::u16string &path) {
            if (auto seg = kern->pull_codeseg_by_ep(romimg.header.entry_point)) {
                return seg;
            }

            auto analyser = arm::make_analyser(arm::arm_disassembler_backend::capstone,
                [&](const vaddress offset) {
                    return *reinterpret_cast<std::uint32_t *>(mem->get_real_pointer(offset));
                });

            std::unordered_map<vaddress, arm::arm_function> funcs;
            analyser->analyse(funcs, romimg.header.entry_point, romimg.header.code_address + romimg.header.code_size);

            LOG_TRACE("Analyzing {}, found {} functions", common::ucs2_to_utf8(path), funcs.size());

            kernel::codeseg_create_info info;

            info.full_path = path;
            info.uids[0] = romimg.header.uid1;
            info.uids[1] = romimg.header.uid2;
            info.uids[2] = romimg.header.uid3;
            info.code_base = romimg.header.code_address;
            info.data_base = romimg.header.data_address;
            info.code_load_addr = romimg.header.code_address;
            info.data_load_addr = romimg.header.data_address;
            info.code_size = romimg.header.code_size;
            info.data_size = romimg.header.data_size;
            info.entry_point = romimg.header.entry_point;
            info.bss_size = romimg.header.bss_size;
            info.export_table = romimg.exports;
            info.sinfo.caps_u[0] = romimg.header.sec_info.cap1;
            info.sinfo.caps_u[1] = romimg.header.sec_info.cap2;
            info.sinfo.vendor_id = romimg.header.sec_info.vendor_id;
            info.sinfo.secure_id = romimg.header.sec_info.secure_id;
            info.exception_descriptor = romimg.header.exception_des;

            auto cs = kern->create<kernel::codeseg>("codeseg", info);

            register_exports(
                common::ucs2_to_utf8(eka2l1::replace_extension(eka2l1::filename(path), u"")),
                cs->get_export_table());

            struct dll_ref_table {
                uint16_t flags;
                uint16_t num_entries;
                uint32_t rom_img_headers_ref[25];
            };

            // Find dependencies
            std::function<void(loader::rom_image_header *, codeseg_ptr)> dig_dependencies;
            dig_dependencies = [&](loader::rom_image_header *header, codeseg_ptr acs) {
                if (header->dll_ref_table_address != 0) {
                    dll_ref_table *ref_table = eka2l1::ptr<dll_ref_table>(header->dll_ref_table_address).get(mem);

                    for (uint16_t i = 0; i < ref_table->num_entries; i++) {
                        // Dig UID
                        loader::rom_image_header *ref_header = eka2l1::ptr<loader::rom_image_header>(ref_table->rom_img_headers_ref[i])
                                                                   .get(mem);

                        if (auto ref_seg = kern->pull_codeseg_by_ep(ref_header->entry_point)) {
                            // Add ref
                            acs->add_dependency(ref_seg);
                        } else {
                            // TODO: Supply right size. The loader doesn't care about size right now
                            common::ro_buf_stream buf_stream(eka2l1::ptr<std::uint8_t>(ref_table->rom_img_headers_ref[i]).get(mem), 
                                0xFFFF);

                            // Load new romimage and add dependency
                            loader::romimg rimg = *loader::parse_romimg(reinterpret_cast<common::ro_stream*>(&buf_stream), mem);
                            acs->add_dependency(load_as_romimg(rimg));
                        }
                    }
                }
            };

            dig_dependencies(&romimg.header, cs);

            return cs;
        }

        std::pair<std::optional<loader::e32img>, std::optional<loader::romimg>> lib_manager::try_search_and_parse(const std::u16string &path) {
            std::u16string lib_path = path;

            // Try opening e32img, if fail, try open as romimg
            auto open_and_get = [&](const std::u16string &path) -> std::pair<std::optional<loader::e32img>, std::optional<loader::romimg>> {
                std::pair<std::optional<loader::e32img>, std::optional<loader::romimg>>
                    result{ std::nullopt, std::nullopt };

                if (io->exist(lib_path)) {
                    symfile f = io->open_file(path, READ_MODE | BIN_MODE);
                    if (!f) {
                        return result;
                    }

                    eka2l1::ro_file_stream image_data_stream(f);

                    auto parse_result = loader::parse_e32img(reinterpret_cast<common::ro_stream*>(&image_data_stream));
                    if (parse_result != std::nullopt) {
                        f->close();
                        result.first = std::move(parse_result);

                        return result;
                    }

                    image_data_stream.seek(0, common::seek_where::beg);
                    auto parse_result_2 = loader::parse_romimg(reinterpret_cast<common::ro_stream*>(&image_data_stream), mem);
                    if (parse_result_2 != std::nullopt) {
                        f->close();
                        result.second = std::move(parse_result_2);

                        return result;
                    }

                    f->close();
                }

                return std::pair<std::optional<loader::e32img>, std::optional<loader::romimg>>{};
            };

            if (!eka2l1::has_root_dir(lib_path)) {
                // Nope ? We need to cycle through all possibilities
                for (drive_number drv = drive_z; drv >= drive_a; drv = static_cast<drive_number>(static_cast<int>(drv) - 1)) {
                    lib_path = drive_to_char16(drv);
                    lib_path += u":\\Sys\\Bin\\";
                    lib_path += path;

                    auto result = open_and_get(lib_path);
                    if (result.first != std::nullopt || result.second != std::nullopt) {
                        return result;
                    }
                }

                return std::pair<std::optional<loader::e32img>, std::optional<loader::romimg>>{};
            }

            return open_and_get(lib_path);
        }

        codeseg_ptr lib_manager::load(const std::u16string &name) {
            auto load_depend_on_drive = [&](drive_number drv, const std::u16string &lib_path) -> codeseg_ptr {
                auto entry = io->get_drive_entry(drv);

                if (entry) {
                    symfile f = io->open_file(lib_path, READ_MODE | BIN_MODE);
                    if (!f) {
                        return nullptr;
                    }
                
                    eka2l1::ro_file_stream image_data_stream(f);

                    if (entry->media_type == drive_media::rom && io->is_entry_in_rom(lib_path)) {
                        auto romimg = loader::parse_romimg(reinterpret_cast<common::ro_stream*>(&image_data_stream), mem);
                        if (!romimg) {
                            return nullptr;
                        }

                        return load_as_romimg(*romimg, lib_path);
                    } else {
                        auto e32img = loader::parse_e32img(reinterpret_cast<common::ro_stream*>(&image_data_stream));
                        if (!e32img) {
                            return nullptr;
                        }

                        return load_as_e32img(*e32img, lib_path);
                    }
                }

                return nullptr;
            };

            std::u16string lib_path = name;

            // Create a new codeseg, we should try search these files
            // Absolute yet ?
            if (!eka2l1::has_root_dir(lib_path)) {
                // Nope ? We need to cycle through all possibilities
                for (drive_number drv = drive_z; drv >= drive_a; drv = static_cast<drive_number>(static_cast<int>(drv) - 1)) {
                    lib_path = drive_to_char16(drv);
                    lib_path += u":\\Sys\\Bin\\";
                    lib_path += name;

                    if (io->exist(lib_path)) {
                        auto result = load_depend_on_drive(drv, lib_path);
                        if (result != nullptr) {
                            result->set_full_path(lib_path);
                            return result;
                        }
                    }
                }

                return nullptr;
            }

            drive_number drv = char16_to_drive(lib_path[0]);
            if (!io->exist(lib_path)) {
                return nullptr;
            }

            if (auto cs = load_depend_on_drive(drv, lib_path)) {
                cs->set_full_path(lib_path);
                return cs;
            }

            return nullptr;
        }

        void lib_manager::shutdown() {
            reset();
        }

        void lib_manager::reset() {
            svc_funcs.clear();
        }

        bool lib_manager::call_svc(sid svcnum) {
            auto res = svc_funcs.find(svcnum);

            if (res == svc_funcs.end()) {
                return false;
            }

            epoc_import_func func = res->second;

            if (sys->get_config()->log_svc) {
                LOG_TRACE("Calling SVC 0x{:x} {}", svcnum, func.name);
            }

#ifdef ENABLE_SCRIPTING
            sys->get_manager_system()->get_script_manager()->call_svcs(svcnum, 0);
#endif

            func.func(sys);

#ifdef ENABLE_SCRIPTING
            sys->get_manager_system()->get_script_manager()->call_svcs(svcnum, 1);
#endif

            return true;
        }

        bool lib_manager::register_exports(const std::string &lib_name, export_table &table) {
            std::string lib_name_lower = common::lowercase_string(lib_name);
            auto lib_ite = lib_symbols.find(lib_name_lower);
            if (lib_ite != lib_symbols.end()) {
                for (std::size_t i = 0; i < common::min(table.size(), lib_ite->second.size()); i++) {
                    addr_symbols.emplace(table[i] & ~0x1, lib_ite->second[i]);
                }

#ifndef ENABLE_SCRIPTING
                return true;
#endif
            }

#ifdef ENABLE_SCRIPTING
            sys->get_manager_system()->get_script_manager()->patch_library_hook(lib_name_lower, table);
            return true;
#else
            return false;
#endif
        }
    }
}
