#include <algorithm>
#include <cerrno>
#include <cassert>
#include <ciso646>
#include <string>
#include <system_error>
#include <vector>
#include <map>

#include <fmt/core.h>
#include <fmt/format.h>
#include <lfp/lfp.h>
#include <lfp/rp66.h>
#include <lfp/tapeimage.h>

#include <dlisio/file.hpp>
#include <dlisio/exception.hpp>

#include <dlisio/dlis/dlisio.h>
#include <dlisio/dlis/types.h>
#include <dlisio/dlis/io.hpp>

namespace dl = dlisio::dlis;

namespace dlisio { namespace dlis {

dlisio::stream open(const std::string& path, std::int64_t offset)
noexcept (false) {
    auto* file = dlisio::fopen(path.c_str());
    if (!file) {
        auto msg = "unable to open file for path {} : {}";
        throw dlisio::io_error(fmt::format(msg, path, strerror(errno)));
    }

    auto* protocol = lfp_cfile_open_at_offset(file, offset);
    if ( protocol == nullptr ) {
        std::fclose(file);
        const auto msg = "lfp: unable to open lfp protocol cfile at tell {}";
        throw dlisio::io_error(fmt::format(msg, offset));
    }

    return dlisio::stream(protocol);
}

dlisio::stream open_rp66(const dlisio::stream& f) noexcept (false) {
    auto* protocol = lfp_rp66_open(f.protocol());
    if ( protocol == nullptr ) {
        if ( lfp_eof(f.protocol()) )
            throw dlisio::eof_error("lfp: cannot open file past eof");
        else
            throw dlisio::io_error("lfp: unable to apply rp66 protocol");
    }

    return dlisio::stream(protocol);
}

dlisio::stream open_tapeimage(const dlisio::stream& f) noexcept (false) {
    auto* protocol = lfp_tapeimage_open(f.protocol());
    if ( protocol == nullptr ) {
        if ( lfp_eof(f.protocol()) )
            throw dlisio::eof_error("lfp: cannot open file past eof");
        else
            throw dlisio::io_error("lfp: unable to apply tapeimage protocol");
    }
    return dlisio::stream(protocol);
}

namespace {

long long findsul( dlisio::stream& file, int toread ) noexcept (false) {
    long long offset;

    const auto lfrom = file.ltell();
    const auto pfrom = file.ptell();

    std::vector<char> buffer;
    buffer.reserve( toread );
    auto bytes_read = file.read(buffer.data(), toread);

    const auto err = dlis_find_sul(buffer.data(), bytes_read, &offset);

    switch (err) {
        case DLIS_OK:
            return lfrom + offset;

        case DLIS_NOTFOUND: {
            auto msg =
                "searched {} bytes from offset {} (dec), but could not find "
                "storage label";
            throw dlisio::not_found(fmt::format(msg, bytes_read, pfrom));
        }

        case DLIS_INCONSISTENT: {
            auto msg = "found something that could be parts of a SUL, "
                       "file may be corrupted";
            throw std::runtime_error(msg);
        }

        default:
            throw std::runtime_error("dlis_find_sul: unknown error");
    }
}

long long findvrl( dlisio::stream& file, int toread ) noexcept (false) {
    long long offset;

    const auto lfrom = file.ltell();
    const auto pfrom = file.ptell();

    std::vector<char> buffer;
    buffer.reserve( toread );
    auto bytes_read = file.read(buffer.data(), toread);

    const auto err = dlis_find_vrl(buffer.data(), bytes_read, &offset);

    // TODO: error messages could maybe be pulled from core library
    switch (err) {
        case DLIS_OK:
            return lfrom + offset;

        case DLIS_NOTFOUND: {
            const auto msg =
                "searched {} bytes from offset {} (dec), but could not find "
                "visible record envelope pattern [0xFF 0x01]";
            throw dlisio::not_found(fmt::format(msg, bytes_read, pfrom));
        }

        case DLIS_INCONSISTENT: {
            const auto msg = "found [0xFF 0x01] but len field not intact, "
                             "file may be corrupted";
            throw std::runtime_error(msg);
        }

        default:
            throw std::runtime_error("dlis_find_vrl: unknown error");
    }
}

} //namespace

void findsul(dlisio::stream& file, const dl::error_handler& errorhandler,
             bool expected) noexcept(false) {
    const auto lfrom = file.ltell();
    const auto pfrom = file.ptell();
    assert (lfrom == 0);

    long long offset;
    try {
        /* In most cases files are well-formed and we already are positioned
         * on the SUL. Hence reading the minimum required to access needle is
         * enough.
         */
        constexpr auto minread = 15;
        offset = findsul(file, minread);
        assert(offset == lfrom);
    } catch (std::exception& e) {
        /* Find late SUL. For performance reasons attempt reading more bytes
         * only if SUL is expected. */
        if (!expected) {
            throw e;
        }
        // default maxread value comes from real files with trash before SUL
        constexpr auto maxread = 1700;
        file.seek(lfrom);
        offset = findsul(file, maxread);

        assert(offset > lfrom);
        const auto debug = "SUL found at ptell {} (dec), but expected at {}";
        errorhandler.log(
            dl::error_severity::MINOR,
            "dlis::findsul: Searching for SUL",
            "Unexpected bytes found before SUL",
            "2.3.2 Storage Unit Label (SUL): The first 80 bytes of the Visible "
                "Envelope ... constitute a Storage Unit Label.",
            "Unexpected bytes are ignored",
            fmt::format(debug, pfrom + (offset - lfrom), pfrom));
    }
    file.seek(offset);
}

void findvrl(dlisio::stream& file,
             const dl::error_handler& errorhandler) noexcept(false) {
    const auto lfrom = file.ltell();
    const auto pfrom = file.ptell();

    long long offset;
    try {
        /* Expected situation: we are positioned right before VR */
        constexpr auto minread = 4;
        offset = findvrl(file, minread);
        assert(offset == lfrom);
    } catch (std::exception& e) {
        /* Compliance with previous dlisio versions: search 200 bytes for VR.
         * No real file that is saved by this check is known, but it might
         * happen exactly because this check is present.
         */
        constexpr auto maxread = 200;
        file.seek(lfrom);
        offset = findvrl(file, maxread);

        assert(offset > lfrom);
        const auto debug = "VR found at ptell {} (dec), but expected at {}";
        errorhandler.log(
            dl::error_severity::MINOR,
            "dlis::findvrl: Searching for VR",
            "Unexpected bytes found before VR",
            "",
            "Unexpected bytes ignored",
            fmt::format(debug, pfrom + (offset - lfrom), pfrom));
    }
    file.seek(offset);
}

bool record::isexplicit() const noexcept (true) {
    return this->attributes & DLIS_SEGATTR_EXFMTLR;
}

bool record::isencrypted() const noexcept (true) {
    return this->attributes & DLIS_SEGATTR_ENCRYPT;
}

namespace {

template < typename T >
bool attr_consistent( const T& ) noexcept (true) {
    // TODO: implement
    // internal attributes should have both successor and predecessor
    // first only successors, last only predecessors
    return true;
}

template < typename T >
bool type_consistent( const T& ) noexcept (true) {
    // TODO: implement
    // should be all-equal
    return true;
}

void trim_segment(std::uint8_t attrs,
                  const char* begin,
                  int segment_size,
                  std::vector< char >& segment,
                  const dl::error_handler& errorhandler)
noexcept (false) {
    int trim = 0;
    const auto* end = begin + segment_size;
    const auto err = dlis_trim_record_segment(attrs, begin, end, &trim);

    switch (err) {
        case DLIS_OK:
            segment.resize(segment.size() - trim);
            return;

        case DLIS_BAD_SIZE: {
            if (trim - segment_size != DLIS_LRSH_SIZE) {
                const auto msg =
                    "bad segment trim: trim size (which is {}) "
                    ">= segment.size() (which is {})";
                throw std::runtime_error(fmt::format(msg, trim, segment_size));
            }

            errorhandler.log(
                dl::error_severity::MINOR,
                "extract (trim_segment)",
                "trim size (padbytes + checksum + trailing length) = logical "
                    "record segment length",
                "[from 2.2.2.1 Logical Record Segment Header (LRSH) and "
                    "2.2.2.4 Logical Record Segment Trailer (LRST) situation "
                    "should be impossible]",
                "Segment is skipped", "");

            segment.resize(segment.size() - segment_size);
            return;
        }

        default:
            throw std::invalid_argument("dlis_trim_record_segment");
    }
}

}

/*
 * store attributes in a string to use the short-string optimisation if
 * available. Just before commit, these are checked for consistency, i.e.
 * that segments don't report inconsistent information on encryption and
 * formatting.
 */
template < typename T >
using shortvec = std::basic_string< T >;


record extract(dlisio::stream& file, long long tell, error_handler& errorhandler)
noexcept (false) {
    record rec;
    rec.data.reserve( 8192 );
    auto nbytes = std::numeric_limits< std::int64_t >::max();
    return extract(file, tell, nbytes, rec, errorhandler);
}

record& extract(dlisio::stream& file, long long tell, long long bytes, record& rec,
                error_handler& errorhandler) noexcept (false) {
    shortvec< std::uint8_t > attributes;
    shortvec< int > types;
    bool consistent = true;

    rec.data.clear();
    file.seek(tell);

    while (true) {
        char buffer[ DLIS_LRSH_SIZE ];
        auto nread = file.read( buffer, DLIS_LRSH_SIZE );
        if ( nread < DLIS_LRSH_SIZE )
            throw std::runtime_error("extract: unable to read LRSH, file truncated");

        int len, type;
        std::uint8_t attrs;
        dlis_lrsh( buffer, &len, &attrs, &type );

        len -= DLIS_LRSH_SIZE;

        attributes.push_back( attrs );
        types.push_back( type );

        const long long prevsize = rec.data.size();
        const auto remaining = bytes - prevsize;

        auto to_read = len;
        /*
         * If the remaining bytes-to-read is less than the full LRS, we
         * can get away with reading a partial LRS as long as there is no
         * padding, checksum or trailing length.
         */
        if ( not (attrs & DLIS_SEGATTR_PADDING) and
             not (attrs & DLIS_SEGATTR_TRAILEN) and
             not (attrs & DLIS_SEGATTR_CHCKSUM) and
             remaining < len ) {

            to_read = remaining;
        }

        rec.data.resize( prevsize + to_read );

        nread = file.read(rec.data.data() + prevsize, to_read);
        if ( nread < to_read )
            throw std::runtime_error("extract: unable to read LRS, file truncated");

        /*
         * chop off trailing length and checksum for now
         * TODO: verify integrity by checking trailing length
         * TODO: calculate checksum
         */

        const auto* fst = rec.data.data() + prevsize;
        trim_segment(attrs, fst, len, rec.data, errorhandler);

        /* if the whole segment is getting trimmed, it's unclear if successor
         * attribute should be erased or not.  For now ignoring.  Suspecting
         * issue will never occur as whole "too many padbytes" problem might be
         * caused by encryption
         */

        const auto has_successor = attrs & DLIS_SEGATTR_SUCCSEG;
        const long long bytes_left = bytes - rec.data.size();
        if (has_successor and bytes_left > 0) continue;

        /*
         * The record type only cares about encryption and formatting, so only
         * extract those for checking consistency. Nothing else is interesting
         * to users, as it only describes how to read this specific segment
         */
        static const auto fmtenc = DLIS_SEGATTR_EXFMTLR | DLIS_SEGATTR_ENCRYPT;
        rec.attributes = attributes.front() & fmtenc;
        rec.type = types.front();

        rec.consistent = consistent;
        if (not attr_consistent( attributes )) rec.consistent = false;
        if (not type_consistent( types ))      rec.consistent = false;
        if (bytes_left < 0) rec.data.resize(bytes);
        return rec;
    }
}

stream_offsets findoffsets( dlisio::stream& file, dl::error_handler& errorhandler)
noexcept (false) {
    stream_offsets ofs;

    std::int64_t lr_offset = 0;
    std::int64_t lrs_offset = 0;

    bool has_successor = false;
    char buffer[ DLIS_LRSH_SIZE ];

    const auto handle = [&]( const std::string& problem ) {
        const auto context = "dlis::findoffsets (indexing logical file)";

        const auto debug = "Physical tell: {} (dec), "
                           "Logical Record tell: {} (dec), "
                           "Logical Record Segment tell: {} (dec)";

        errorhandler.log(
            dl::error_severity::CRITICAL, context, problem, "",
            "Indexing is suspended at last valid Logical Record",
            fmt::format(debug, file.ptell(), lr_offset, lrs_offset));

        ofs.broken.push_back( lr_offset );
    };

    int len = 0;
    auto read = 0;

    file.seek(lrs_offset);

    while (true) {
        try {
            read = file.read(buffer, DLIS_LRSH_SIZE);
        } catch (std::exception& e) {
            handle(e.what());
            break;
        }

        // read operation is enough to set eof correctly
        if (file.eof()) {
            if (read == 0) {
                if (has_successor) {
                    const auto problem =
                        "Reached EOF, but last logical record segment expects "
                        "successor";
                    handle(problem);
                }
                break;
            }
            if (read < 4) {
                /*
                 * Very unlikely situation.
                 * Usually exception will be thrown during read.
                 */
                const auto problem =
                    "File truncated in Logical Record Header";
                handle(problem);
                break;
            }
            /*
             * Do nothing if read == 4
             * This might be the problem for next Logical File.
             * If not, it will be dealt with later
             */
        }

        int type;
        std::uint8_t attrs;
        dlis_lrsh( buffer, &len, &attrs, &type );
        if (len < 4) {
            const auto problem =
                "Too short logical record. Length can't be less than 4, "
                "but was {}";
            handle(fmt::format(problem, len));
            break;
        }

        bool isexplicit      = attrs & DLIS_SEGATTR_EXFMTLR;
        bool has_predecessor = attrs & DLIS_SEGATTR_PREDSEG;

        if (not (has_predecessor)) {
            if (isexplicit and type == 0 and ofs.explicits.size()) {
                /*
                 * Wrap up when we encounter a EFLR of type FILE-HEADER that
                 * is NOT the first Logical Record. More precisely we expect
                 * the _first_ LR we encounter to be a FILE-HEADER. We
                 * gather up this LR and all successive LR's until we
                 * encounter another FILE-HEADER.
                 */

                if (has_successor) {
                    const auto problem =
                        "End of logical file, but last logical "
                        "record segment expects successor";
                    handle(problem);
                    break;
                }

                // seek to assure handle is in the right place to read next LF
                file.seek( lrs_offset );
                break;
            }
        }

        has_successor = attrs & DLIS_SEGATTR_SUCCSEG;
        lrs_offset += len;

        /*
         * Skip the segment by moving the cursor to the next offset.
         * Seek operation alone isn't enough to correctly set EOF. To make sure
         * record is not truncated, read its last byte instead of seeking
         * to the new offset.
         *
         * Note that lfp returns UNEXPECTED_EOF for cfile when truncation
         * happens inside of declared data
         * TODO: assure behavior for other io types
         */

        char tmp;
        try {
            file.seek(lrs_offset - 1);
        } catch (std::exception& e) {
            handle(e.what());
            break;
        }
        try {
            file.read(&tmp, 1);
        } catch (const std::runtime_error& e) {
            const auto problem = "File truncated in Logical Record Segment";
            handle(problem);
            break;
        }


        if (not (has_successor)) {
            if (isexplicit)
                ofs.explicits.push_back( lr_offset );
            /*
             * Consider doing fdata-indexing on the fly as we are now at the
             * correct offset to read the OBNAME. That would mean we only
             * need to traverse the file a single time to index it.
             * Additionally it would make the caller code from python way
             * nicer.
             */
            else
                ofs.implicits.push_back( lr_offset );

            lr_offset = lrs_offset;
        }
    }
    return ofs;
}

std::map< dl::ident, std::vector< long long > >
findfdata(dlisio::stream& file, const std::vector< long long >& tells,
dl::error_handler& errorhandler) noexcept (false) {
    std::map< dl::ident, std::vector< long long > > xs;

    constexpr std::size_t OBNAME_SIZE_MAX = 262;

    record rec;
    rec.data.reserve( OBNAME_SIZE_MAX );

    const auto handle = [&]( const std::string& problem ) {
        const auto context = "dlis::findfdata: Indexing implicit records";
        const auto abs_tell = "Physical tell (end of the record): {} (dec)";
        const auto debug = fmt::format(abs_tell, file.ptell());
        errorhandler.log(dl::error_severity::CRITICAL, context, problem, "",
                         "Record is skipped", debug);
    };

    for (auto tell : tells) {
        try {
            extract(file, tell, OBNAME_SIZE_MAX, rec, errorhandler);
        } catch (std::exception& e) {
            handle(e.what());
            continue;
        }

        if (rec.isencrypted()) continue;
        if (rec.type != 0 and rec.type != 1) continue;
        if (rec.data.size() == 0) continue;

        int32_t origin;
        uint8_t copy;
        int32_t idlen;
        char id[ 256 ];
        const char* cur = dlis_obname(rec.data.data(), &origin, &copy, &idlen, id);

        std::size_t obname_size = cur - rec.data.data();
        if (obname_size > rec.data.size()) {
            const auto problem =
                "fdata record corrupted, error on reading obname";
            handle(problem);
            continue;
        }
        dl::obname tmp{ dl::origin{ origin },
                        dl::ushort{ copy },
                        dl::ident{ std::string{ id, id + idlen } } };

        if (rec.type == 0)
            xs[tmp.fingerprint("FRAME")].push_back( tell );
        if (rec.type == 1)
            xs[tmp.fingerprint("NO-FORMAT")].push_back( tell );
    }
    return xs;
}

} // namespace dlis

} // namespace dlisio
