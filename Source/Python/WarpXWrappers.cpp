/* Copyright 2019 Andrew Myers, Axel Huebl, David Grote
 * Luca Fedeli, Maxence Thevenet, Remi Lehe
 * Weiqun Zhang
 *
 * This file is part of WarpX.
 *
 * License: BSD-3-Clause-LBNL
 */
#include "BoundaryConditions/PML.H"
#include "Initialization/WarpXAMReXInit.H"
#include "Particles/MultiParticleContainer.H"
#include "Particles/ParticleBoundaryBuffer.H"
#include "Particles/WarpXParticleContainer.H"
#include "Utils/WarpXUtil.H"
#include "WarpX.H"
#include "WarpXWrappers.h"
#include "WarpX_py.H"

#include <AMReX.H>
#include <AMReX_ArrayOfStructs.H>
#include <AMReX_Box.H>
#include <AMReX_FArrayBox.H>
#include <AMReX_FabArray.H>
#include <AMReX_Geometry.H>
#include <AMReX_GpuControl.H>
#include <AMReX_IndexType.H>
#include <AMReX_IntVect.H>
#include <AMReX_MFIter.H>
#include <AMReX_MultiFab.H>
#include <AMReX_PODVector.H>
#include <AMReX_ParIter.H>
#include <AMReX_Particles.H>
#include <AMReX_StructOfArrays.H>

#include <array>
#include <cstdlib>

namespace
{
    amrex::Real** getMultiFabPointers(const amrex::MultiFab& mf, int *num_boxes, int *ncomps, int **ngrowvect, int **shapes)
    {
        *ncomps = mf.nComp();
        *num_boxes = mf.local_size();
        int shapesize = AMREX_SPACEDIM;
        *ngrowvect = static_cast<int*>(malloc(sizeof(int)*shapesize));
        for (int j = 0; j < AMREX_SPACEDIM; ++j) {
            (*ngrowvect)[j] = mf.nGrow(j);
        }
        if (mf.nComp() > 1) shapesize += 1;
        *shapes = static_cast<int*>(malloc(sizeof(int)*shapesize * (*num_boxes)));
        auto data =
            static_cast<amrex::Real**>(malloc((*num_boxes) * sizeof(amrex::Real*)));

#ifdef AMREX_USE_OMP
#pragma omp parallel if (amrex::Gpu::notInLaunchRegion())
#endif
        for ( amrex::MFIter mfi(mf, false); mfi.isValid(); ++mfi ) {
            int i = mfi.LocalIndex();
            data[i] = (amrex::Real*) mf[mfi].dataPtr();
            for (int j = 0; j < AMREX_SPACEDIM; ++j) {
                (*shapes)[shapesize*i+j] = mf[mfi].box().length(j);
            }
            if (mf.nComp() > 1) (*shapes)[shapesize*i+AMREX_SPACEDIM] = mf.nComp();
        }
        return data;
    }
    int* getMultiFabLoVects(const amrex::MultiFab& mf, int *num_boxes, int **ngrowvect)
    {
        int shapesize = AMREX_SPACEDIM;
        *ngrowvect = static_cast<int*>(malloc(sizeof(int)*shapesize));
        for (int j = 0; j < AMREX_SPACEDIM; ++j) {
            (*ngrowvect)[j] = mf.nGrow(j);
        }
        *num_boxes = mf.local_size();
        int *loVects = (int*) malloc((*num_boxes)*AMREX_SPACEDIM * sizeof(int));

        int i = 0;
        for ( amrex::MFIter mfi(mf, false); mfi.isValid(); ++mfi, ++i ) {
            const int* loVect = mf[mfi].loVect();
            for (int j = 0; j < AMREX_SPACEDIM; ++j) {
                loVects[AMREX_SPACEDIM*i+j] = loVect[j];
            }
        }
        return loVects;
    }
    // Copy the nodal flag data and return the copy:
    // the nodal flag data should not be modifiable from Python.
    int* getFieldNodalFlagData ( const amrex::MultiFab& mf )
    {
        const amrex::IntVect nodal_flag( mf.ixType().toIntVect() );
        int *nodal_flag_data = (int*) malloc(AMREX_SPACEDIM * sizeof(int));

        constexpr int NODE = amrex::IndexType::NODE;

        for (int i=0 ; i < AMREX_SPACEDIM ; i++) {
            nodal_flag_data[i] = (nodal_flag[i] == NODE ? 1 : 0);
        }
        return nodal_flag_data;
    }
}

extern "C"
{

    int warpx_Real_size()
    {
        return (int)sizeof(amrex::Real);
    }

    int warpx_ParticleReal_size()
    {
        return (int)sizeof(amrex::ParticleReal);
    }

    int warpx_nSpecies()
    {
        const auto & mypc = WarpX::GetInstance().GetPartContainer();
        return mypc.nSpecies();
    }

    bool warpx_use_fdtd_nci_corr()
    {
        return WarpX::use_fdtd_nci_corr;
    }

    int warpx_galerkin_interpolation()
    {
        return WarpX::galerkin_interpolation;
    }

    int warpx_nComps()
    {
        return PIdx::nattribs;
    }

    int warpx_nCompsSpecies(const char* char_species_name)
    {
        auto & mypc = WarpX::GetInstance().GetPartContainer();
        const std::string species_name(char_species_name);
        auto & myspc = mypc.GetParticleContainerFromName(species_name);
        return myspc.NumRealComps();
    }

    int warpx_SpaceDim()
    {
        return AMREX_SPACEDIM;
    }

    void amrex_init (int argc, char* argv[])
    {
        warpx_amrex_init(argc, argv);
    }

    void amrex_init_with_inited_mpi (int argc, char* argv[], MPI_Comm mpicomm)
    {
        warpx_amrex_init(argc, argv, true, mpicomm);
    }

    void amrex_finalize (int /*finalize_mpi*/)
    {
        amrex::Finalize();
    }

    void warpx_init ()
    {
        WarpX& warpx = WarpX::GetInstance();
        warpx.InitData();
        if (warpx_py_afterinit) warpx_py_afterinit();
        if (warpx_py_particleloader) warpx_py_particleloader();
    }

    void warpx_finalize ()
    {
        WarpX::ResetInstance();
    }

    void warpx_set_callback_py_afterinit (WARPX_CALLBACK_PY_FUNC_0 callback)
    {
        warpx_py_afterinit = callback;
    }
    void warpx_set_callback_py_beforeEsolve (WARPX_CALLBACK_PY_FUNC_0 callback)
    {
        warpx_py_beforeEsolve = callback;
    }
    void warpx_set_callback_py_poissonsolver (WARPX_CALLBACK_PY_FUNC_0 callback)
    {
        warpx_py_poissonsolver = callback;
    }
    void warpx_set_callback_py_afterEsolve (WARPX_CALLBACK_PY_FUNC_0 callback)
    {
        warpx_py_afterEsolve = callback;
    }
    void warpx_set_callback_py_beforedeposition (WARPX_CALLBACK_PY_FUNC_0 callback)
    {
        warpx_py_beforedeposition = callback;
    }
    void warpx_set_callback_py_afterdeposition (WARPX_CALLBACK_PY_FUNC_0 callback)
    {
        warpx_py_afterdeposition = callback;
    }
    void warpx_set_callback_py_particlescraper (WARPX_CALLBACK_PY_FUNC_0 callback)
    {
        warpx_py_particlescraper = callback;
    }
    void warpx_set_callback_py_particleloader (WARPX_CALLBACK_PY_FUNC_0 callback)
    {
        warpx_py_particleloader = callback;
    }
    void warpx_set_callback_py_beforestep (WARPX_CALLBACK_PY_FUNC_0 callback)
    {
        warpx_py_beforestep = callback;
    }
    void warpx_set_callback_py_afterstep (WARPX_CALLBACK_PY_FUNC_0 callback)
    {
        warpx_py_afterstep = callback;
    }
    void warpx_set_callback_py_afterrestart (WARPX_CALLBACK_PY_FUNC_0 callback)
    {
        warpx_py_afterrestart = callback;
    }
    void warpx_set_callback_py_particleinjection (WARPX_CALLBACK_PY_FUNC_0 callback)
    {
        warpx_py_particleinjection = callback;
    }
    void warpx_set_callback_py_appliedfields (WARPX_CALLBACK_PY_FUNC_0 callback)
    {
        warpx_py_appliedfields = callback;
    }

    void warpx_evolve (int numsteps)
    {
        WarpX& warpx = WarpX::GetInstance();
        warpx.Evolve(numsteps);
    }

    void warpx_addNParticles(
        const char* char_species_name, int lenx, amrex::ParticleReal const * x,
        amrex::ParticleReal const * y, amrex::ParticleReal const * z,
        amrex::ParticleReal const * vx, amrex::ParticleReal const * vy,
        amrex::ParticleReal const * vz, int nattr,
        amrex::ParticleReal const * attr, int uniqueparticles)
    {
        auto & mypc = WarpX::GetInstance().GetPartContainer();
        const std::string species_name(char_species_name);
        auto & myspc = mypc.GetParticleContainerFromName(species_name);
        const int lev = 0;
        myspc.AddNParticles(lev, lenx, x, y, z, vx, vy, vz, nattr, attr, uniqueparticles);
    }

    void warpx_ConvertLabParamsToBoost()
    {
      ConvertLabParamsToBoost();
    }

    void warpx_ReadBCParams()
    {
      ReadBCParams();
    }

    void warpx_CheckGriddingForRZSpectral()
    {
      CheckGriddingForRZSpectral();
    }

    amrex::Real warpx_getProbLo(int dir)
    {
      WarpX& warpx = WarpX::GetInstance();
      const amrex::Geometry& geom = warpx.Geom(0);
      return geom.ProbLo(dir);
    }

    amrex::Real warpx_getProbHi(int dir)
    {
      WarpX& warpx = WarpX::GetInstance();
      const amrex::Geometry& geom = warpx.Geom(0);
      return geom.ProbHi(dir);
    }

    amrex::Real warpx_getCellSize(int dir, int lev) {
        const std::array<amrex::Real,3>& dx = WarpX::CellSize(lev);
        return dx[dir];
    }

    long warpx_getNumParticles(const char* char_species_name) {
        const auto & mypc = WarpX::GetInstance().GetPartContainer();
        const std::string species_name(char_species_name);
        auto & myspc = mypc.GetParticleContainerFromName(species_name);
        return myspc.TotalNumberOfParticles();
    }

#define WARPX_GET_FIELD(FIELD, GETTER) \
    amrex::Real** FIELD(int lev, int direction, \
                        int *return_size, int *ncomps, int **ngrowvect, int **shapes) { \
        auto & mf = GETTER(lev, direction); \
        return getMultiFabPointers(mf, return_size, ncomps, ngrowvect, shapes); \
    }

#define WARPX_GET_LOVECTS(FIELD, GETTER) \
    int* FIELD(int lev, int direction, \
               int *return_size, int **ngrowvect) { \
        auto & mf = GETTER(lev, direction); \
        return getMultiFabLoVects(mf, return_size, ngrowvect); \
    }

    WARPX_GET_FIELD(warpx_getEfield, WarpX::GetInstance().getEfield)
    WARPX_GET_FIELD(warpx_getEfieldCP, WarpX::GetInstance().getEfield_cp)
    WARPX_GET_FIELD(warpx_getEfieldFP, WarpX::GetInstance().getEfield_fp)

    WARPX_GET_FIELD(warpx_getBfield, WarpX::GetInstance().getBfield)
    WARPX_GET_FIELD(warpx_getBfieldCP, WarpX::GetInstance().getBfield_cp)
    WARPX_GET_FIELD(warpx_getBfieldFP, WarpX::GetInstance().getBfield_fp)

    WARPX_GET_FIELD(warpx_getCurrentDensity, WarpX::GetInstance().getcurrent)
    WARPX_GET_FIELD(warpx_getCurrentDensityCP, WarpX::GetInstance().getcurrent_cp)
    WARPX_GET_FIELD(warpx_getCurrentDensityFP, WarpX::GetInstance().getcurrent_fp)

    WARPX_GET_LOVECTS(warpx_getEfieldLoVects, WarpX::GetInstance().getEfield)
    WARPX_GET_LOVECTS(warpx_getEfieldCPLoVects, WarpX::GetInstance().getEfield_cp)
    WARPX_GET_LOVECTS(warpx_getEfieldFPLoVects, WarpX::GetInstance().getEfield_fp)

    WARPX_GET_LOVECTS(warpx_getBfieldLoVects, WarpX::GetInstance().getBfield)
    WARPX_GET_LOVECTS(warpx_getBfieldCPLoVects, WarpX::GetInstance().getBfield_cp)
    WARPX_GET_LOVECTS(warpx_getBfieldFPLoVects, WarpX::GetInstance().getBfield_fp)

    WARPX_GET_LOVECTS(warpx_getCurrentDensityLoVects, WarpX::GetInstance().getcurrent)
    WARPX_GET_LOVECTS(warpx_getCurrentDensityCPLoVects, WarpX::GetInstance().getcurrent_cp)
    WARPX_GET_LOVECTS(warpx_getCurrentDensityFPLoVects, WarpX::GetInstance().getcurrent_fp)

    int* warpx_getEx_nodal_flag()  {return getFieldNodalFlagData( WarpX::GetInstance().getEfield(0,0) );}
    int* warpx_getEy_nodal_flag()  {return getFieldNodalFlagData( WarpX::GetInstance().getEfield(0,1) );}
    int* warpx_getEz_nodal_flag()  {return getFieldNodalFlagData( WarpX::GetInstance().getEfield(0,2) );}
    int* warpx_getBx_nodal_flag()  {return getFieldNodalFlagData( WarpX::GetInstance().getBfield(0,0) );}
    int* warpx_getBy_nodal_flag()  {return getFieldNodalFlagData( WarpX::GetInstance().getBfield(0,1) );}
    int* warpx_getBz_nodal_flag()  {return getFieldNodalFlagData( WarpX::GetInstance().getBfield(0,2) );}
    int* warpx_getJx_nodal_flag()  {return getFieldNodalFlagData( WarpX::GetInstance().getcurrent(0,0) );}
    int* warpx_getJy_nodal_flag()  {return getFieldNodalFlagData( WarpX::GetInstance().getcurrent(0,1) );}
    int* warpx_getJz_nodal_flag()  {return getFieldNodalFlagData( WarpX::GetInstance().getcurrent(0,2) );}
    int* warpx_getRho_nodal_flag() {return getFieldNodalFlagData( WarpX::GetInstance().getrho_fp(0) );}

#define WARPX_GET_SCALAR(SCALAR, GETTER) \
    amrex::Real** SCALAR(int lev, \
                         int *return_size, int *ncomps, int **ngrowvect, int **shapes) { \
        auto & mf = GETTER(lev); \
        return getMultiFabPointers(mf, return_size, ncomps, ngrowvect, shapes); \
    }

#define WARPX_GET_LOVECTS_SCALAR(SCALAR, GETTER) \
    int* SCALAR(int lev, \
                int *return_size, int **ngrowvect) { \
        auto & mf = GETTER(lev); \
        return getMultiFabLoVects(mf, return_size, ngrowvect); \
    }

    WARPX_GET_SCALAR(warpx_getChargeDensityCP, WarpX::GetInstance().getrho_cp)
    WARPX_GET_SCALAR(warpx_getChargeDensityFP, WarpX::GetInstance().getrho_fp)

    WARPX_GET_LOVECTS_SCALAR(warpx_getChargeDensityCPLoVects, WarpX::GetInstance().getrho_cp)
    WARPX_GET_LOVECTS_SCALAR(warpx_getChargeDensityFPLoVects, WarpX::GetInstance().getrho_fp)

#define WARPX_GET_FIELD_PML(FIELD, GETTER) \
    amrex::Real** FIELD(int lev, int direction, \
                        int *return_size, int *ncomps, int **ngrowvect, int **shapes) { \
        auto * pml = WarpX::GetInstance().GetPML(lev); \
        if (pml) { \
            auto & mf = *(pml->GETTER()[direction]); \
            return getMultiFabPointers(mf, return_size, ncomps, ngrowvect, shapes); \
        } else { \
            return nullptr; \
        } \
    }

#define WARPX_GET_LOVECTS_PML(FIELD, GETTER) \
    int* FIELD(int lev, int direction, \
               int *return_size, int **ngrowvect) { \
        auto * pml = WarpX::GetInstance().GetPML(lev); \
        if (pml) { \
            auto & mf = *(pml->GETTER()[direction]); \
            return getMultiFabLoVects(mf, return_size, ngrowvect); \
        } else { \
            return nullptr; \
        } \
    }

    WARPX_GET_FIELD_PML(warpx_getEfieldCP_PML, GetE_cp)
    WARPX_GET_FIELD_PML(warpx_getEfieldFP_PML, GetE_fp)
    WARPX_GET_FIELD_PML(warpx_getBfieldCP_PML, GetB_cp)
    WARPX_GET_FIELD_PML(warpx_getBfieldFP_PML, GetB_fp)
    WARPX_GET_FIELD_PML(warpx_getCurrentDensityCP_PML, Getj_cp)
    WARPX_GET_FIELD_PML(warpx_getCurrentDensityFP_PML, Getj_fp)
    WARPX_GET_LOVECTS_PML(warpx_getEfieldCPLoVects_PML, GetE_cp)
    WARPX_GET_LOVECTS_PML(warpx_getEfieldFPLoVects_PML, GetE_fp)
    WARPX_GET_LOVECTS_PML(warpx_getBfieldCPLoVects_PML, GetB_cp)
    WARPX_GET_LOVECTS_PML(warpx_getBfieldFPLoVects_PML, GetB_fp)
    WARPX_GET_LOVECTS_PML(warpx_getCurrentDensityCPLoVects_PML, Getj_cp)
    WARPX_GET_LOVECTS_PML(warpx_getCurrentDensityFPLoVects_PML, Getj_fp)

    amrex::ParticleReal** warpx_getParticleStructs(
            const char* char_species_name, int lev,
            int* num_tiles, int** particles_per_tile) {
        const auto & mypc = WarpX::GetInstance().GetPartContainer();
        const std::string species_name(char_species_name);
        auto & myspc = mypc.GetParticleContainerFromName(species_name);

        int i = 0;
        for (WarpXParIter pti(myspc, lev); pti.isValid(); ++pti, ++i) {}

        // *num_tiles = myspc.numLocalTilesAtLevel(lev);
        *num_tiles = i;
        *particles_per_tile = (int*) malloc(*num_tiles*sizeof(int));

        amrex::ParticleReal** data = (amrex::ParticleReal**) malloc(*num_tiles*sizeof(typename WarpXParticleContainer::ParticleType*));
        i = 0;
        for (WarpXParIter pti(myspc, lev); pti.isValid(); ++pti, ++i) {
            auto& aos = pti.GetArrayOfStructs();
            data[i] = (amrex::ParticleReal*) aos.data();
            (*particles_per_tile)[i] = pti.numParticles();
        }
        return data;
    }

    amrex::ParticleReal** warpx_getParticleArrays (
            const char* char_species_name, const char* char_comp_name,
            int lev, int* num_tiles, int** particles_per_tile ) {

        const auto & mypc = WarpX::GetInstance().GetPartContainer();
        const std::string species_name(char_species_name);
        auto & myspc = mypc.GetParticleContainerFromName(species_name);

        int comp = warpx_getParticleCompIndex(char_species_name, char_comp_name);

        int i = 0;
        for (WarpXParIter pti(myspc, lev); pti.isValid(); ++pti, ++i) {}

        // *num_tiles = myspc.numLocalTilesAtLevel(lev);
        *num_tiles = i;
        *particles_per_tile = (int*) malloc(*num_tiles*sizeof(int));

        amrex::ParticleReal** data = (amrex::ParticleReal**) malloc(*num_tiles*sizeof(amrex::ParticleReal*));
        i = 0;
        for (WarpXParIter pti(myspc, lev); pti.isValid(); ++pti, ++i) {
            auto& soa = pti.GetStructOfArrays();
            data[i] = (amrex::ParticleReal*) soa.GetRealData(comp).dataPtr();
            (*particles_per_tile)[i] = pti.numParticles();
        }
        return data;
    }

    int warpx_getParticleCompIndex (
         const char* char_species_name, const char* char_comp_name )
    {
        const auto & mypc = WarpX::GetInstance().GetPartContainer();

        const std::string species_name(char_species_name);
        auto & myspc = mypc.GetParticleContainerFromName(species_name);

        const std::string comp_name(char_comp_name);
        auto particle_comps = myspc.getParticleComps();

        return particle_comps.at(comp_name);
    }

    void warpx_addRealComp(const char* char_species_name,
        const char* char_comp_name, bool comm=true)
    {
        auto & mypc = WarpX::GetInstance().GetPartContainer();
        const std::string species_name(char_species_name);
        auto & myspc = mypc.GetParticleContainerFromName(species_name);

        const std::string comp_name(char_comp_name);
        myspc.AddRealComp(comp_name, comm);

        mypc.defineAllParticleTiles();
    }

    int warpx_getParticleBoundaryBufferSize(const char* species_name, int boundary)
    {
        const std::string name(species_name);
        auto& particle_buffers = WarpX::GetInstance().GetParticleBoundaryBuffer();
        return particle_buffers.getNumParticlesInContainer(species_name, boundary);
    }

    int** warpx_getParticleBoundaryBufferScrapedSteps(const char* species_name, int boundary, int lev,
                     int* num_tiles, int** particles_per_tile)
    {
        const std::string name(species_name);
        auto& particle_buffers = WarpX::GetInstance().GetParticleBoundaryBuffer();
        auto& particle_buffer = particle_buffers.getParticleBuffer(species_name, boundary);

        const int comp = particle_buffer.NumIntComps() - 1;

        int i = 0;
        for (amrex::ParIter<0,0,PIdx::nattribs, 0, amrex::PinnedArenaAllocator> pti(particle_buffer, lev); pti.isValid(); ++pti, ++i) {}

        // *num_tiles = myspc.numLocalTilesAtLevel(lev);
        *num_tiles = i;
        *particles_per_tile = (int*) malloc(*num_tiles*sizeof(int));

        int** data = (int**) malloc(*num_tiles*sizeof(int*));
        i = 0;
        for (amrex::ParIter<0,0,PIdx::nattribs, 0, amrex::PinnedArenaAllocator> pti(particle_buffer, lev); pti.isValid(); ++pti, ++i) {
            auto& soa = pti.GetStructOfArrays();
            data[i] = (int*) soa.GetIntData(comp).dataPtr();
            (*particles_per_tile)[i] = pti.numParticles();
        }

        return data;
    }

    amrex::ParticleReal** warpx_getParticleBoundaryBuffer(const char* species_name, int boundary, int lev,
                     int* num_tiles, int** particles_per_tile, const char* comp_name)
    {
        const std::string name(species_name);
        auto& particle_buffers = WarpX::GetInstance().GetParticleBoundaryBuffer();
        auto& particle_buffer = particle_buffers.getParticleBuffer(species_name, boundary);

        const int comp = warpx_getParticleCompIndex(species_name, comp_name);

        int i = 0;
        for (amrex::ParIter<0,0,PIdx::nattribs, 0, amrex::PinnedArenaAllocator> pti(particle_buffer, lev); pti.isValid(); ++pti, ++i) {}

        // *num_tiles = myspc.numLocalTilesAtLevel(lev);
        *num_tiles = i;
        *particles_per_tile = (int*) malloc(*num_tiles*sizeof(int));

        amrex::ParticleReal** data = (amrex::ParticleReal**) malloc(*num_tiles*sizeof(amrex::ParticleReal*));
        i = 0;
        for (amrex::ParIter<0,0,PIdx::nattribs, 0, amrex::PinnedArenaAllocator> pti(particle_buffer, lev); pti.isValid(); ++pti, ++i) {
            auto& soa = pti.GetStructOfArrays();
            data[i] = (amrex::ParticleReal*) soa.GetRealData(comp).dataPtr();
            (*particles_per_tile)[i] = pti.numParticles();
        }

        return data;
    }

    void warpx_clearParticleBoundaryBuffer () {
        auto& particle_buffers = WarpX::GetInstance().GetParticleBoundaryBuffer();
        particle_buffers.clearParticles();
    }

    void warpx_ComputeDt () {
        WarpX& warpx = WarpX::GetInstance();
        warpx.ComputeDt ();
    }
    void warpx_MoveWindow (int step,bool move_j) {
        WarpX& warpx = WarpX::GetInstance();
        warpx.MoveWindow (step, move_j);
    }

    void warpx_EvolveE (amrex::Real dt) {
        WarpX& warpx = WarpX::GetInstance();
        warpx.EvolveE (dt);
    }
    void warpx_EvolveB (amrex::Real dt, DtType a_dt_type) {
        WarpX& warpx = WarpX::GetInstance();
        warpx.EvolveB (dt, a_dt_type);
    }
    void warpx_FillBoundaryE () {
        WarpX& warpx = WarpX::GetInstance();
        warpx.FillBoundaryE (warpx.getngE());
    }
    void warpx_FillBoundaryB () {
        WarpX& warpx = WarpX::GetInstance();
        warpx.FillBoundaryB (warpx.getngE());
    }
    void warpx_SyncCurrent () {
        WarpX& warpx = WarpX::GetInstance();
        warpx.SyncCurrent ();
    }
    void warpx_UpdateAuxilaryData () {
        WarpX& warpx = WarpX::GetInstance();
        warpx.UpdateAuxilaryData ();
    }
    void warpx_PushParticlesandDepose (amrex::Real cur_time) {
        WarpX& warpx = WarpX::GetInstance();
        warpx.PushParticlesandDepose (cur_time);
    }

    int warpx_getistep (int lev) {
        WarpX& warpx = WarpX::GetInstance();
        return warpx.getistep (lev);
    }
    void warpx_setistep (int lev, int ii) {
        WarpX& warpx = WarpX::GetInstance();
        warpx.setistep (lev, ii);
    }
    amrex::Real warpx_gett_new (int lev) {
        WarpX& warpx = WarpX::GetInstance();
        return warpx.gett_new (lev);
    }
    void warpx_sett_new (int lev, amrex::Real time) {
        WarpX& warpx = WarpX::GetInstance();
        warpx.sett_new (lev, time);
    }
    amrex::Real warpx_getdt (int lev) {
        WarpX& warpx = WarpX::GetInstance();
        return warpx.getdt (lev);
    }

    int warpx_maxStep () {
        WarpX& warpx = WarpX::GetInstance();
        return warpx.maxStep ();
    }
    amrex::Real warpx_stopTime () {
        WarpX& warpx = WarpX::GetInstance();
        return warpx.stopTime ();
    }

    int warpx_finestLevel () {
        WarpX& warpx = WarpX::GetInstance();
        return warpx.finestLevel ();
    }

    int warpx_getMyProc () {
        return amrex::ParallelDescriptor::MyProc();
    }

    int warpx_getNProcs () {
        return amrex::ParallelDescriptor::NProcs();
    }

    void mypc_Redistribute () {
        auto & mypc = WarpX::GetInstance().GetPartContainer();
        mypc.Redistribute();
    }

}
