/******************************************************************************
* Copyright (c) 2012, Howard Butler, hobu.inc@gmail.com
* Copyright (c) 2013, Paul Ramsey, pramsey@cleverelephant.ca
*
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following
* conditions are met:
*
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above copyright
*       notice, this list of conditions and the following disclaimer in
*       the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of Hobu, Inc. or Flaxen Geo Consulting nor the
*       names of its contributors may be used to endorse or promote
*       products derived from this software without specific prior
*       written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
* "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
* LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
* FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
* COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
* INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
* BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
* OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
* AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
* OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
* OF SUCH DAMAGE.
****************************************************************************/

#include <pdal/drivers/pgpointcloud/Writer.hpp>
#include <pdal/PointBuffer.hpp>
#include <pdal/StageFactory.hpp>
#include <pdal/pdal_macros.hpp>
#include <pdal/FileUtils.hpp>
#include <pdal/Endian.hpp>

#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>

#include <sstream>

#ifdef USE_PDAL_PLUGIN_PGPOINTCLOUD
MAKE_WRITER_CREATOR(pgpointcloudWriter, pdal::drivers::pgpointcloud::Writer)
CREATE_WRITER_PLUGIN(pgpointcloud, pdal::drivers::pgpointcloud::Writer)
#endif

// TO DO: 
// - PCID / Schema consistency. If a PCID is specified,
// must it be consistent with the buffer schema? Or should
// the writer shove the data into the database schema as best
// it can?
// - Load information table. Should PDAL write into a metadata
// table information about each load? If so, how to distinguish
// between loads? Leave to pre/post SQL?


namespace pdal
{
namespace drivers
{
namespace pgpointcloud
{

Writer::Writer(Stage& prevStage, const Options& options)
    : pdal::Writer(prevStage, options)
    , m_session(0)
    , m_pdal_schema(prevStage.getSchema())
    , m_schema_name("")
    , m_table_name("")
    , m_column_name("")
    , m_patch_compression_type(COMPRESSION_NONE)
    , m_patch_capacity(400)
    , m_srid(0)
    , m_pcid(0)
    , m_have_postgis(false)
    , m_create_index(true)
    , m_overwrite(true)
    , m_sdo_pc_is_initialized(false)
{


    return;
}


Writer::~Writer()
{
    return;
}


//
// Called from PDAL core during start-up. Do everything
// here that you are going to absolutely require later.
// Optional things you can defer or attempt to initialize 
// here.
//
void Writer::initialize()
{
    pdal::Writer::initialize();
    
    // If we don't know the table name, we're SOL
    m_table_name = getOptions().getValueOrThrow<std::string>("table");

    // Schema and column name can be defaulted safely
    m_column_name = getOptions().getValueOrDefault<std::string>("column", "pa");
    m_schema_name = getOptions().getValueOrDefault<std::string>("schema", "");
    
    // Read compression type and turn into an integer
    std::string compression_str = getOptions().getValueOrDefault<std::string>("compression", "dimensional");
    m_patch_compression_type = getCompressionType(compression_str);

    // Connection string needs to exist and actually work
    std::string connection = getOptions().getValueOrDefault<std::string>("connection", "");
    
    // No string, nothing we can do
    if ( ! connection.size() )
    {
        throw soci_driver_error("unable to connect to database, no connection string was given!");
    }

    // Can we connect, using this string?
    try
    {
        m_session = new ::soci::session(::soci::postgresql, connection);
        log()->get(logDEBUG) << "Connected to database" << std::endl;
    } 
    catch (::soci::soci_error const &e)
    {
        std::stringstream oss;
        oss << "Unable to connect '" << connection << "' with error '" << e.what() << "'";
        throw pdal_error(oss.str());
    }

    // Direct database log info to the logger
    m_session->set_log_stream(&(log()->get(logDEBUG2)));

    // 

    // Read other preferences
    m_overwrite = getOptions().getValueOrDefault<bool>("overwrite", true);
    m_patch_capacity = getOptions().getValueOrDefault<boost::uint32_t>("capacity", 400);
    m_srid = getOptions().getValueOrDefault<boost::uint32_t>("srid", 4326);
    m_pcid = getOptions().getValueOrDefault<boost::uint32_t>("pcid", 0);

    return;
}

//
// Called from somewhere (?) in PDAL core presumably to provide a user-friendly
// means of editing the reader options.
//
Options Writer::getDefaultOptions()
{
    Options options;

    Option table("table", "", "table to write to");
    Option schema("schema", "", "schema table resides in");
    Option column("column", "", "column to write to");
    Option compression("compression", "dimensional", "patch compression format to use (none, dimensional, ght)");
    Option overwrite("overwrite", true, "replace any existing table");
    Option capacity("capacity", 400, "how many points to store in each patch");
    Option srid("srid", 4326, "spatial reference id to store data in");
    Option pcid("pcid", 0, "use this existing pointcloud schema id, if it exists");
    Option pre_sql("pre_sql", "", "before the pipeline runs, read and execute this SQL file, or run this SQL command");
    Option post_sql("post_sql", "", "after the pipeline runs, read and execute this SQL file, or run this SQL command");

    options.add(table);
    options.add(schema);
    options.add(column);
    options.add(compression);
    options.add(overwrite);
    options.add(capacity);
    options.add(srid);
    options.add(pcid);
    options.add(pre_sql);
    options.add(post_sql);

    return options;
}

//
// Called by PDAL core before the start of the writing process, but 
// after the initialization. At this point, the machinery is all set
// up and we can apply actions to the target database, like pre-SQL and
// preparing new tables and/or deleting old ones.
//
void Writer::writeBegin(boost::uint64_t /*targetNumPointsToWrite*/)
{

    // Start up the database connection
    m_session->begin();

    // Pre-SQL can be *either* a SQL file to execute, *or* a SQL statement
    // to execute. We find out which one here.
    std::string pre_sql = getOptions().getValueOrDefault<std::string>("pre_sql", "");
    if (pre_sql.size())
    {
        std::string sql = FileUtils::readFileAsString(pre_sql);
        if (!sql.size())
        {
            // if there was no file to read because the data in pre_sql was 
            // actually the sql code the user wanted to run instead of the 
            // filename to open, we'll use that instead.
            sql = pre_sql;
        }
        m_session->once << sql;
    }

    bool bHaveTable = CheckTableExists(m_table_name);
    
    // Apply the over-write preference if it is set
    if ( m_overwrite && bHaveTable )
    {
        DeleteTable(m_schema_name, m_table_name);
        bHaveTable = false;
    }

    // Read or create a PCID for our new table
    m_pcid = SetupSchema(m_pdal_schema, m_srid);

    // Create the table! 
    if ( ! bHaveTable )
    {
        CreateTable(m_schema_name, m_table_name, m_column_name, m_pcid);
    }
        
    return;
}

void Writer::writeEnd(boost::uint64_t /*actualNumPointsWritten*/)
{
    if ( m_create_index && m_have_postgis )
    {
        CreateIndex(m_schema_name, m_table_name, m_column_name);
    }

    // Post-SQL can be *either* a SQL file to execute, *or* a SQL statement
    // to execute. We find out which one here.
    std::string post_sql = getOptions().getValueOrDefault<std::string>("post_sql", "");
    if (post_sql.size())
    {
        std::string sql = FileUtils::readFileAsString(post_sql);
        if (!sql.size())
        {
            // if there was no file to read because the data in post_sql was 
            // actually the sql code the user wanted to run instead of the 
            // filename to open, we'll use that instead.
            sql = post_sql;
        }
        m_session->once << sql;
    }

    m_session->commit();
    return;
}


boost::uint32_t Writer::SetupSchema(Schema const& buffer_schema, boost::uint32_t srid)
{
    // We strip any ignored dimensions from the schema before creating the table
    pdal::Schema output_schema(PackSchema(buffer_schema));
        
    // If the user has specified a PCID they want to use,
    // does it exist in the database?
    std::ostringstream oss;    
    long schema_count;
    if ( m_pcid )
    {
        oss << "SELECT Count(pcid) FROM pointcloud_formats WHERE pcid = " << m_pcid;
        m_session->once << oss.str(), ::soci::into(schema_count);
        oss.str("");
        if ( schema_count == 0 )
        {
            oss << "requested PCID '" << m_pcid << "' does not exist in POINTCLOUD_FORMATS";
            throw pdal_error(oss.str());
        }
        return m_pcid;
    }

    // Do we have any existing schemas in the POINTCLOUD_FORMATS table?
    boost::uint32_t pcid;
    bool bCreatePCPointSchema = true;
    oss << "SELECT Count(pcid) FROM pointcloud_formats";
    m_session->once << oss.str(), ::soci::into(schema_count);
    oss.str("");
    
    // Do any of the existing schemas match the one we want to use?
    if (schema_count > 0)
    {
        std::vector<std::string> pg_schemas(schema_count);
        std::vector<long> pg_schema_ids(schema_count);
        m_session->once << "SELECT pcid, schema FROM pointcloud_formats", ::soci::into(pg_schema_ids), ::soci::into(pg_schemas);
        
        for(int i=0; i<schema_count; ++i)
        {
            if (pdal::Schema::from_xml(pg_schemas[i]) == output_schema)
            {
                bCreatePCPointSchema = false;
                pcid = pg_schema_ids[i];
                break;
            }
        }
    }
    
    if (bCreatePCPointSchema)
    {
        std::string xml;
        std::string compression;

        if (schema_count == 0)
        {
            pcid = 1;
        } 
        else
        {
            m_session->once << "SELECT Max(pcid)+1 AS pcid FROM pointcloud_formats", 
                               ::soci::into(pcid);
        }  

        /* If the writer specifies a compression, we should set that */
        if ( m_patch_compression_type == COMPRESSION_DIMENSIONAL )
        {
            compression = "dimensional";
        }
        else if ( m_patch_compression_type == COMPRESSION_GHT )
        {
            compression = "ght";
        }

        Metadata metadata("compression", compression, "");
        xml = pdal::Schema::to_xml(output_schema, &(metadata.toPTree()));       
        // xml = pdal::Schema::to_xml(output_schema);       
        oss << "INSERT INTO pointcloud_formats (pcid, srid, schema) ";
        oss << "VALUES (:pcid, :srid, :xml)";

        m_session->once << oss.str(), ::soci::use(pcid, "pcid"), ::soci::use(srid, "srid"), ::soci::use(xml, "xml");
        oss.str("");
    }
    m_pcid = pcid;
    return m_pcid;   
}


void Writer::DeleteTable(std::string const& schema_name,
                         std::string const& table_name)
{
    std::ostringstream oss;

    oss << "DROP TABLE IF EXISTS ";

    if ( schema_name.size() )
    {
        oss << schema_name << ".";
    }
    oss << table_name;

    m_session->once << oss.str();
    oss.str("");
}

Schema Writer::PackSchema( Schema const& schema) const
{
    schema::index_by_index const& idx = schema.getDimensions().get<schema::index>();
    log()->get(logDEBUG3) << "Packing ignored dimension from PointBuffer " << std::endl;

    boost::uint32_t position(0);
    
    pdal::Schema clean_schema;
    schema::index_by_index::size_type i(0);
    for (i = 0; i < idx.size(); ++i)
    {
        if (! idx[i].isIgnored())
        {
            
            Dimension d(idx[i]);
            d.setPosition(position);
            
            // Wipe off parent/child relationships if we're ignoring 
            // same-named dimensions
            d.setParent(boost::uuids::nil_uuid());
            clean_schema.appendDimension(d);
            position++;
        }
    }
    std::string xml = pdal::Schema::to_xml(clean_schema);
    return clean_schema;
}

bool Writer::CheckPointCloudExists()
{
    std::ostringstream oss;
    oss << "SELECT PC_Version()";

    log()->get(logDEBUG) << "checking for pointcloud existence ... " << std::endl;

    try 
    {  
        m_session->once << oss.str();
    } 
    catch (::soci::soci_error const &e)
    {
        oss.str("");
        return false;
    } 

    oss.str("");
    return true;
}

bool Writer::CheckPostGISExists()
{
    std::ostringstream oss;
    oss << "SELECT PostGIS_Version()";

    log()->get(logDEBUG) << "checking for PostGIS existence ... " << std::endl;

    try 
    {  
        m_session->once << oss.str();
    } 
    catch (::soci::soci_error const &e)
    {
        oss.str("");
        return false;
    } 

    oss.str("");
    return true;
}


bool Writer::CheckTableExists(std::string const& name)
{

    std::ostringstream oss;
    oss << "SELECT tablename FROM pg_tables";

    log()->get(logDEBUG) << "checking for " << name << " existence ... " << std::endl;

    ::soci::rowset<std::string> rs = (m_session->prepare << oss.str());

    std::ostringstream debug;
    for (::soci::rowset<std::string>::const_iterator it = rs.begin(); it != rs.end(); ++it)
    {
        debug << ", " << *it;
        if (boost::iequals(*it, name))
        {
            log()->get(logDEBUG) << "it exists!" << std::endl;
            return true;
        }
    }
    log()->get(logDEBUG) << debug.str();
    log()->get(logDEBUG) << " -- '" << name << "' not found." << std::endl;

    return false;
}

void Writer::CreateTable(std::string const& schema_name, 
                         std::string const& table_name,
                         std::string const& column_name,
                         boost::uint32_t pcid)
{
    std::ostringstream oss;
    oss << "CREATE TABLE ";
    if ( schema_name.size() )
    {
        oss << schema_name << ".";
    }
    oss << table_name;
    oss << " (id SERIAL PRIMARY KEY, " << column_name << " PcPatch";
    if ( pcid )
    {
        oss << "(" << pcid << ")";
    }
    oss << ")";

    m_session->once << oss.str();
    oss.str("");
}

// Make sure you test for the presence of PostGIS before calling this
void Writer::CreateIndex(std::string const& schema_name, 
                         std::string const& table_name, 
                         std::string const& column_name)
{
    std::ostringstream oss;

    oss << "CREATE INDEX ";
    if ( schema_name.size() )
    {
        oss << schema_name << "_";
    }
    oss << table_name << "_pc_gix";
    oss << " USING GIST (Geometry(" << column_name << "))";

    m_session->once << oss.str();
    oss.str("");        
}


//
// Called by PDAL core before *each buffer* is written.
// So it gets called a lot. The hack below does something
// the first time it is called only. Hopefully we do 
// not need that hack anymore.
//
void Writer::writeBufferBegin(PointBuffer const& data)
{
    if ( ! m_sdo_pc_is_initialized) 
    {
        // Currently Unused
        // Do somethine only once, after PointBuffer is sent in
        // like setting up tables, for example, in case the
        // schema we get from the parent is not valid?
        m_sdo_pc_is_initialized = true;
    }

    return;
}



boost::uint32_t Writer::writeBuffer(const PointBuffer& buffer)
{
    boost::uint32_t numPoints = buffer.getNumPoints();

    WriteBlock(buffer);

    return numPoints;
}

bool Writer::WriteBlock(PointBuffer const& buffer)
{
    boost::uint8_t* point_data;
    boost::uint32_t point_data_length;
    boost::uint32_t schema_byte_size;
    
    PackPointData(buffer, &point_data, point_data_length, schema_byte_size);
    
    // Pluck the block id out of the first point in the buffer
    pdal::Schema const& schema = buffer.getSchema();
    Dimension const& blockDim = schema.getDimension("BlockID");
    
//    boost::int32_t blk_id  = buffer.getField<boost::int32_t>(blockDim, 0);
    boost::uint32_t num_points = static_cast<boost::uint32_t>(buffer.getNumPoints());
    
    if ( num_points > m_patch_capacity )
    {
        // error here
    }

    std::vector<boost::uint8_t> block_data;
    for (boost::uint32_t i = 0; i < point_data_length; ++i)
    {
        block_data.push_back(point_data[i]);
    }
    
    /* We are always getting uncompressed bytes off the block_data */
    /* so we always used compression type 0 (uncompressed) in writing our WKB */
    boost::int32_t pcid = m_pcid;
    boost::uint32_t compression = COMPRESSION_NONE;
    
    std::stringstream oss;
    oss << "INSERT INTO " << m_table_name << " (pa) VALUES (:hex)";
    
    std::stringstream options;
    #ifdef BOOST_LITTLE_ENDIAN
        options << boost::format("%02x") % 1;
        SWAP_ENDIANNESS(pcid);
        SWAP_ENDIANNESS(compression);
        SWAP_ENDIANNESS(num_points);
    #elif BOOST_BIG_ENDIAN
        options << boost::format("%02x") % 0;
    #endif
    
    options << boost::format("%08x") % pcid;
    options << boost::format("%08x") % compression;
    options << boost::format("%08x") % num_points;

    std::stringstream hex;
    hex << options.str() << Utils::binary_to_hex_string(block_data);
    ::soci::statement st = (m_session->prepare << oss.str(), ::soci::use(hex.str(),"hex"));
    st.execute(true);
    oss.str("");

    return true;
}


void Writer::PackPointData(PointBuffer const& buffer,
                           boost::uint8_t** point_data,
                           boost::uint32_t& point_data_len,
                           boost::uint32_t& schema_byte_size)

{
    // Creates a new buffer that has the ignored dimensions removed from 
    // it.
    
    schema::index_by_index const& idx = buffer.getSchema().getDimensions().get<schema::index>();

    schema_byte_size = 0;
    schema::index_by_index::size_type i(0);
    for (i = 0; i < idx.size(); ++i)
    {
        if (! idx[i].isIgnored())
            schema_byte_size = schema_byte_size+idx[i].getByteSize();
    }
    
    log()->get(logDEBUG) << "Packed schema byte size " << schema_byte_size << std::endl;;

    point_data_len = buffer.getNumPoints() * schema_byte_size;
    *point_data = new boost::uint8_t[point_data_len];
    
    boost::uint8_t* current_position = *point_data;
    
    for (boost::uint32_t i = 0; i < buffer.getNumPoints(); ++i)
    {
        boost::uint8_t* data = buffer.getData(i);
        for (boost::uint32_t d = 0; d < idx.size(); ++d)
        {
            if (! idx[d].isIgnored())
            {
                memcpy(current_position, data, idx[d].getByteSize());
                current_position = current_position+idx[d].getByteSize();
            }
            data = data + idx[d].getByteSize();
                
        }
    }

    
}

}
}
} // namespaces
