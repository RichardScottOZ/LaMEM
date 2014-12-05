//---------------------------------------------------------------------------
//.................   FDSTAG PARAVIEW XML OUTPUT ROUTINES   .................
//---------------------------------------------------------------------------
#include "LaMEM.h"
#include "fdstag.h"
#include "solVar.h"
#include "scaling.h"
#include "tssolve.h"
#include "bc.h"
#include "JacRes.h"
#include "paraViewOutBin.h"
#include "outFunct.h"
#include "Utils.h"
//---------------------------------------------------------------------------
// * phase-ratio output
// * integrate AVD phase viewer
//---------------------------------------------------------------------------
//............................. Output buffer ...............................
//---------------------------------------------------------------------------
#undef __FUNCT__
#define __FUNCT__ "OutBufCreate"
PetscErrorCode OutBufCreate(OutBuf *outbuf, JacRes *jr)
{
	FDSTAG   *fs;
	PetscInt rx, ry, rz, sx, sy, sz, nx, ny, nz;

	PetscErrorCode ierr;
	PetscFunctionBegin;

	fs = jr->fs;

	// clear object
	ierr = PetscMemzero(outbuf, sizeof(OutBuf)); CHKERRQ(ierr);

	// initialize parameters
	outbuf->fs    = fs;
	outbuf->fp    = NULL;
	outbuf->cn    = 0;

	// get local output grid sizes
	GET_OUTPUT_RANGE(rx, nx, sx, fs->dsx)
	GET_OUTPUT_RANGE(ry, ny, sy, fs->dsy)
	GET_OUTPUT_RANGE(rz, nz, sz, fs->dsz)

	// allocate output buffer
	ierr = PetscMalloc((size_t)(_max_num_comp_*nx*ny*nz)*sizeof(float), &outbuf->buff); CHKERRQ(ierr);

	// allocate corner buffers
	ierr = DMCreateLocalVector (fs->DA_COR, &outbuf->lbcor); CHKERRQ(ierr);

	// set pointers to center & edge buffers (reuse from JacRes object)
	outbuf->lbcen = jr->ldxx;
	outbuf->lbxy  = jr->ldxy;
	outbuf->lbxz  = jr->ldxz;
	outbuf->lbyz  = jr->ldyz;

	PetscFunctionReturn(0);
}
//---------------------------------------------------------------------------
#undef __FUNCT__
#define __FUNCT__ "OutBufDestroy"
PetscErrorCode OutBufDestroy(OutBuf *outbuf)
{
	PetscErrorCode ierr;
	PetscFunctionBegin;

	// free output buffer
	ierr = PetscFree(outbuf->buff); CHKERRQ(ierr);

	// free corner buffers
	ierr = VecDestroy(&outbuf->lbcor); CHKERRQ(ierr);

	PetscFunctionReturn(0);
}
//---------------------------------------------------------------------------
void OutBufConnectToFile(OutBuf *outbuf, FILE *fp)
{
	// set file pointer
	outbuf->fp = fp;

	// clear buffer
	outbuf->cn = 0;
}
//---------------------------------------------------------------------------
void OutBufDump(OutBuf *outbuf)
{
	// dump output buffer contents to disk

	int nbytes;

	// compute number of bytes
	nbytes = outbuf->cn*(int)sizeof(float);

	// dump number of bytes
	fwrite(&nbytes, sizeof(int), 1, outbuf->fp);

	// dump buffer contents
	fwrite(outbuf->buff, sizeof(float), (size_t)outbuf->cn, outbuf->fp);

	// clear buffer
	outbuf->cn = 0;
}
//---------------------------------------------------------------------------
void OutBufPutCoordVec(
	OutBuf      *outbuf,
	Discret1D   *ds,
	PetscScalar  cf)  // scaling coefficient
{
	// put FDSTAG coordinate vector to output buffer

	PetscInt    i, r, n, s;
	float       *buff;
	PetscScalar *ncoor;

	// get number of node points for output
	GET_OUTPUT_RANGE(r, n, s, (*ds))

	// access output buffer and coordinate array
	buff  = outbuf->buff;
	ncoor = ds->ncoor;

	// scale & copy to buffer
	for(i = 0; i < n; i++) buff[i] = (float) (cf*ncoor[i]);

	// update number of elements in the buffer
	outbuf->cn += n;

}
//---------------------------------------------------------------------------
#undef __FUNCT__
#define __FUNCT__ "OutBufPut3DVecComp"
PetscErrorCode OutBufPut3DVecComp(
	OutBuf      *outbuf,
	PetscInt     ncomp,  // number of components
	PetscInt     dir,    // component identifier
	PetscScalar  cf,     // scaling coefficient
	PetscScalar  shift) // shift parameter (subtracted from scaled values)
{
	// put component of 3D vector to output buffer
	// component data is taken from obuf->gbcor vector

	FDSTAG      *fs;
	float       *buff;
	PetscScalar ***arr;
	PetscInt    i, j, k, rx, ry, rz, sx, sy, sz, nx, ny, nz, cnt;

	PetscErrorCode ierr;
	PetscFunctionBegin;

	// access grid layout & buffer
	fs   = outbuf->fs;
	buff = outbuf->buff;

	// scatter ghost points to local buffer vector from global source vector
	LOCAL_TO_LOCAL(fs->DA_COR, outbuf->lbcor)

	// access local buffer vector
	ierr = DMDAVecGetArray(fs->DA_COR, outbuf->lbcor, &arr); CHKERRQ(ierr);

	// get sub-domain ranks, starting node IDs, and number of nodes
	GET_OUTPUT_RANGE(rx, nx, sx, fs->dsx)
	GET_OUTPUT_RANGE(ry, ny, sy, fs->dsy)
	GET_OUTPUT_RANGE(rz, nz, sz, fs->dsz)

	// set counter
	cnt = dir;

	// copy vector component to buffer
	if(cf < 0.0)
	{
		// negative scaling -> logarithmic output
		cf = -cf;

		START_STD_LOOP
		{
			// write
			buff[cnt] = (float) PetscLog10Real(cf*arr[k][j][i] - shift);

			// update counter
			cnt += ncomp;
		}
		END_STD_LOOP
	}
	else
	{
		// positive scaling -> standard output

		START_STD_LOOP
		{
			// write
			buff[cnt] = (float) (cf*arr[k][j][i] - shift);

			// update counter
			cnt += ncomp;
		}
		END_STD_LOOP

	}

	// restore access
	ierr = DMDAVecRestoreArray(fs->DA_COR, outbuf->lbcor, &arr); CHKERRQ(ierr);

	// update number of elements in the buffer
	outbuf->cn += nx*ny*nz;

	PetscFunctionReturn(0);
}
//---------------------------------------------------------------------------
//...........  Multi-component output vector data structure .................
//---------------------------------------------------------------------------
void OutVecCreate(
	OutVec         *outvec,
	const char     *name,
	const char     *label,
	OutVecFunctPtr  OutVecFunct,
	PetscInt        ncomp)
{
	// store name
	asprintf(&outvec->name, "%s %s", name, label);

	// output function
	outvec->OutVecFunct = OutVecFunct;

	// number of components
	outvec->ncomp = ncomp;

}
//---------------------------------------------------------------------------
void OutVecDestroy(OutVec *outvec)
{
	LAMEM_FREE(outvec->name);
}
//---------------------------------------------------------------------------
//.......................... Vector output mask .............................
//---------------------------------------------------------------------------
void OutMaskSetDefault(OutMask *omask)
{
	// clear
	memset(omask, 0, sizeof(OutMask));

	omask->phase          = 1;
	omask->viscosity      = 1;
	omask->velocity       = 1;
	omask->pressure       = 1;
}
//---------------------------------------------------------------------------
PetscInt OutMaskCountActive(OutMask *omask)
{
	PetscInt cnt = 0;

	if(omask->phase)          cnt++; // phase
	if(omask->density)        cnt++; // density
	if(omask->viscosity)      cnt++; // effective viscosity
	if(omask->velocity)       cnt++; // velocity
	if(omask->pressure)       cnt++; // pressure
	if(omask->temperature)    cnt++; // temperature
	if(omask->dev_stress)     cnt++; // deviatoric stress tensor
	if(omask->j2_dev_stress)  cnt++; // deviatoric stress second invariant
	if(omask->strain_rate)    cnt++; // deviatoric strain rate tensor
	if(omask->j2_strain_rate) cnt++; // deviatoric strain rate second invariant
	if(omask->vol_rate)       cnt++; // volumetric strain rate
	if(omask->vorticity)      cnt++; // vorticity vector
	if(omask->ang_vel_mag)    cnt++; // average angular velocity magnitude
	if(omask->tot_strain)     cnt++; // total strain
	if(omask->plast_strain)   cnt++; // accumulated plastic strain
	if(omask->plast_dissip)   cnt++; // plastic dissipation
	if(omask->tot_displ)      cnt++; // total displacements
	// === debugging vectors ===============================================
	if(omask->moment_res)     cnt++; // momentum residual
	if(omask->cont_res)       cnt++; // continuity residual
	if(omask->energ_res)      cnt++; // energy residual
	if(omask->DII_CEN)        cnt++; // effective strain rate invariant in center
	if(omask->DII_XY)         cnt++; // effective strain rate invariant on xy-edge
	if(omask->DII_XZ)         cnt++; // effective strain rate invariant on xz-edge
	if(omask->DII_YZ)         cnt++; // effective strain rate invariant on yz-edge

	return cnt;
}
//---------------------------------------------------------------------------
//...................... ParaView output driver object ......................
//---------------------------------------------------------------------------
#undef __FUNCT__
#define __FUNCT__ "PVOutClear"
PetscErrorCode PVOutClear(PVOut *pvout)
{
	PetscErrorCode ierr;
	PetscFunctionBegin;

	// clear object
	ierr = PetscMemzero(pvout, sizeof(PVOut)); CHKERRQ(ierr);

	PetscFunctionReturn(0);
}
//---------------------------------------------------------------------------
#undef __FUNCT__
#define __FUNCT__ "PVOutCreate"
PetscErrorCode PVOutCreate(PVOut *pvout, JacRes *jr, const char *filename)
{
	Scaling  *scal;
	OutMask  *omask;
	OutVec   *outvecs;
	PetscInt  cnt;

	PetscErrorCode ierr;
	PetscFunctionBegin;

	scal = &jr->scal;

	// set file name
	asprintf(&pvout->outfile, "%s", filename);

	// set output scaling for coordinates
	pvout->crdScal = scal->length;

	// get output mask
	omask = &pvout->omask;

	// activate default vectors
	OutMaskSetDefault(omask);

	// create output buffer object
	ierr = OutBufCreate(&pvout->outbuf, jr);

	// set pvd file flag & offset
	pvout->offset = 0;
	pvout->outpvd = 0;

	// read options
	ierr = PVOutReadFromOptions(pvout);

	//===============
	// OUTPUT VECTORS
	//===============

	// count active output vectors
	pvout->nvec = OutMaskCountActive(omask);

	// allocate space
	ierr = PetscMalloc(sizeof(OutVec)*(size_t)pvout->nvec, &pvout->outvecs); CHKERRQ(ierr);

	// access output vectors
	outvecs = pvout->outvecs;

	// set all output functions & collect information to allocate buffers
	cnt = 0;

	if(omask->phase)          OutVecCreate(&outvecs[cnt++], "phase",          scal->lbl_unit,             &PVOutWritePhase,        1);
	if(omask->density)        OutVecCreate(&outvecs[cnt++], "density",        scal->lbl_density,          &PVOutWriteDensity,      1);
	if(omask->viscosity)      OutVecCreate(&outvecs[cnt++], "viscosity",      scal->lbl_viscosity,        &PVOutWriteViscosity,    1);
	if(omask->velocity)       OutVecCreate(&outvecs[cnt++], "velocity",       scal->lbl_velocity,         &PVOutWriteVelocity,     3);
	if(omask->pressure)       OutVecCreate(&outvecs[cnt++], "pressure",       scal->lbl_stress,           &PVOutWritePressure,     1);
	if(omask->temperature)    OutVecCreate(&outvecs[cnt++], "temperature",    scal->lbl_temperature,      &PVOutWriteTemperature,  1);
	if(omask->dev_stress)     OutVecCreate(&outvecs[cnt++], "dev_stress",     scal->lbl_stress,           &PVOutWriteDevStress,    6);
	if(omask->strain_rate)    OutVecCreate(&outvecs[cnt++], "strain_rate",    scal->lbl_strain_rate,      &PVOutWriteStrainRate,   6);
	if(omask->j2_dev_stress)  OutVecCreate(&outvecs[cnt++], "j2_dev_stress",  scal->lbl_stress,           &PVOutWriteJ2DevStress,  1);
	if(omask->j2_strain_rate) OutVecCreate(&outvecs[cnt++], "j2_strain_rate", scal->lbl_strain_rate,      &PVOutWriteJ2StrainRate, 1);
	if(omask->vol_rate)       OutVecCreate(&outvecs[cnt++], "vol_rate",       scal->lbl_strain_rate,      &PVOutWriteVolRate,      1);
	if(omask->vorticity)      OutVecCreate(&outvecs[cnt++], "vorticity",      scal->lbl_strain_rate,      &PVOutWriteVorticity,    3);
	if(omask->ang_vel_mag)    OutVecCreate(&outvecs[cnt++], "ang_vel_mag",    scal->lbl_angular_velocity, &PVOutWriteAngVelMag,    1);
	if(omask->tot_strain)     OutVecCreate(&outvecs[cnt++], "tot_strain",     scal->lbl_unit,             &PVOutWriteTotStrain,    1);
	if(omask->plast_strain)   OutVecCreate(&outvecs[cnt++], "plast_strain",   scal->lbl_unit,             &PVOutWritePlastStrain,  1);
	if(omask->plast_dissip)   OutVecCreate(&outvecs[cnt++], "plast_dissip",   scal->lbl_dissipation_rate, &PVOutWritePlastDissip,  1);
	if(omask->tot_displ)      OutVecCreate(&outvecs[cnt++], "tot_displ",      scal->lbl_length,           &PVOutWriteTotDispl,     3);
	// === debugging vectors ===============================================
	if(omask->moment_res)     OutVecCreate(&outvecs[cnt++], "moment_res",     scal->lbl_force,            &PVOutWriteMomentRes,    3);
	if(omask->cont_res)       OutVecCreate(&outvecs[cnt++], "cont_res",       scal->lbl_strain_rate,      &PVOutWriteContRes,      1);
	if(omask->energ_res)      OutVecCreate(&outvecs[cnt++], "energ_res",      scal->lbl_dissipation_rate, &PVOutWritEnergRes,      1);
	if(omask->DII_CEN)        OutVecCreate(&outvecs[cnt++], "DII_CEN",        scal->lbl_strain_rate,      &PVOutWriteDII_CEN,      1);
	if(omask->DII_XY)         OutVecCreate(&outvecs[cnt++], "DII_XY",         scal->lbl_strain_rate,      &PVOutWriteDII_XY,       1);
	if(omask->DII_XZ)         OutVecCreate(&outvecs[cnt++], "DII_XZ",         scal->lbl_strain_rate,      &PVOutWriteDII_XZ,       1);
	if(omask->DII_YZ)         OutVecCreate(&outvecs[cnt++], "DII_YZ",         scal->lbl_strain_rate,      &PVOutWriteDII_YZ,       1);

	PetscFunctionReturn(0);
}
//---------------------------------------------------------------------------
#undef __FUNCT__
#define __FUNCT__ "PVOutReadFromOptions"
PetscErrorCode PVOutReadFromOptions(PVOut *pvout)
{
	OutMask  *omask;

	PetscErrorCode ierr;
	PetscFunctionBegin;

	// get output mask
	omask = &pvout->omask;

	ierr = PetscOptionsGetInt(NULL, "-out_pvd",            &pvout->outpvd,         NULL); CHKERRQ(ierr);
	ierr = PetscOptionsGetInt(NULL, "-out_phase",          &omask->phase,          NULL); CHKERRQ(ierr);
	ierr = PetscOptionsGetInt(NULL, "-out_density",        &omask->density,        NULL); CHKERRQ(ierr);
	ierr = PetscOptionsGetInt(NULL, "-out_viscosity",      &omask->viscosity,      NULL); CHKERRQ(ierr);
	ierr = PetscOptionsGetInt(NULL, "-out_velocity",       &omask->velocity,       NULL); CHKERRQ(ierr);
	ierr = PetscOptionsGetInt(NULL, "-out_pressure",       &omask->pressure,       NULL); CHKERRQ(ierr);
	ierr = PetscOptionsGetInt(NULL, "-out_temperature",    &omask->temperature,    NULL); CHKERRQ(ierr);
	ierr = PetscOptionsGetInt(NULL, "-out_dev_stress",     &omask->dev_stress,     NULL); CHKERRQ(ierr);
	ierr = PetscOptionsGetInt(NULL, "-out_j2_dev_stress",  &omask->j2_dev_stress,  NULL); CHKERRQ(ierr);
	ierr = PetscOptionsGetInt(NULL, "-out_strain_rate",    &omask->strain_rate,    NULL); CHKERRQ(ierr);
	ierr = PetscOptionsGetInt(NULL, "-out_j2_strain_rate", &omask->j2_strain_rate, NULL); CHKERRQ(ierr);
	ierr = PetscOptionsGetInt(NULL, "-out_vol_rate",       &omask->vol_rate,       NULL); CHKERRQ(ierr);
	ierr = PetscOptionsGetInt(NULL, "-out_vorticity",      &omask->vorticity,      NULL); CHKERRQ(ierr);
	ierr = PetscOptionsGetInt(NULL, "-out_ang_vel_mag",    &omask->ang_vel_mag,    NULL); CHKERRQ(ierr);
	ierr = PetscOptionsGetInt(NULL, "-out_tot_strain",     &omask->tot_strain,     NULL); CHKERRQ(ierr);
	ierr = PetscOptionsGetInt(NULL, "-out_plast_strain",   &omask->plast_strain,   NULL); CHKERRQ(ierr);
	ierr = PetscOptionsGetInt(NULL, "-out_plast_dissip",   &omask->plast_dissip,   NULL); CHKERRQ(ierr);
	ierr = PetscOptionsGetInt(NULL, "-out_tot_displ",      &omask->tot_displ,      NULL); CHKERRQ(ierr);

	if(pvout->outpvd)
	{
		PetscPrintf(PETSC_COMM_WORLD, " Writing .pvd file to disk\n");
	}

	PetscFunctionReturn(0);
}
//---------------------------------------------------------------------------
#undef __FUNCT__
#define __FUNCT__ "PVOutDestroy"
PetscErrorCode PVOutDestroy(PVOut *pvout)
{
	PetscInt i;

	PetscErrorCode ierr;
	PetscFunctionBegin;

	// file name
	LAMEM_FREE(pvout->outfile);

	// output vectors
	for(i = 0; i < pvout->nvec; i++)
		OutVecDestroy(&pvout->outvecs[i]);

	PetscFree(pvout->outvecs);

	// output buffer
	ierr = OutBufDestroy(&pvout->outbuf); CHKERRQ(ierr);

	PetscFunctionReturn(0);
}
//---------------------------------------------------------------------------
void PVOutWriteXMLHeader(FILE *fp, const char *file_type)
{
	// write standard header to ParaView XML file
	fprintf(fp,"<?xml version=\"1.0\"?>\n");
#ifdef PETSC_WORDS_BIGENDIAN
	fprintf(fp,"<VTKFile type=\"%s\" version=\"0.1\" byte_order=\"BigEndian\">\n", file_type);
#else
	fprintf(fp,"<VTKFile type=\"%s\" version=\"0.1\" byte_order=\"LittleEndian\">\n", file_type);
#endif
}
//---------------------------------------------------------------------------
#undef __FUNCT__
#define __FUNCT__ "PVOutWriteTimeStep"
PetscErrorCode PVOutWriteTimeStep(PVOut *pvout, JacRes *jr, const char *dirName, PetscScalar ttime, PetscInt tindx)
{
	PetscErrorCode ierr;
	PetscFunctionBegin;

	// update .pvd file
	ierr = PVOutUpdatePVD(pvout, dirName, ttime, tindx);

	// write parallel data .pvtr file
	ierr = PVOutWritePVTR(pvout, dirName); CHKERRQ(ierr);

	// write sub-domain data .vtr files
	ierr = PVOutWriteVTR(pvout, jr, dirName); CHKERRQ(ierr);

	PetscFunctionReturn(0);
}
//---------------------------------------------------------------------------
#undef __FUNCT__
#define __FUNCT__ "PVOutUpdatePVD"
PetscErrorCode PVOutUpdatePVD(PVOut *pvout, const char *dirName, PetscScalar ttime, PetscInt tindx)
{
	FILE        *fp;
	char        *fname;

	PetscErrorCode ierr;
	PetscFunctionBegin;

	// only first process generates this file (WARNING! Bottleneck!)
	if(!ISRankZero(PETSC_COMM_WORLD)) PetscFunctionReturn(0);

	// open outfile.pvd file (write or update mode)
	asprintf(&fname, "%s.pvd", pvout->outfile);
	if(!tindx) fp = fopen(fname,"w");
	else       fp = fopen(fname,"r+");
	if(fp == NULL) SETERRQ1(PETSC_COMM_WORLD, 1,"cannot open file %s", fname);
	free(fname);

	if(!tindx)
	{
		// write header
		PVOutWriteXMLHeader(fp, "Collection");

		// open time step collection
		fprintf(fp,"<Collection>\n");
	}
	else
	{
		// put the file pointer on the next entry
		ierr = fseek(fp, pvout->offset, SEEK_SET); CHKERRQ(ierr);
	}

	// add entry to .pvd file (outfile.pvtr in the output directory)
	fprintf(fp,"\t<DataSet timestep=\"%1.6e\" file=\"%s/%s.pvtr\"/>\n",
		ttime, dirName, pvout->outfile);

	// store current position in the file
	pvout->offset = ftell(fp);

	// close time step collection
	fprintf(fp,"</Collection>\n");
	fprintf(fp,"</VTKFile>\n");

	// close file
	fclose(fp);

	PetscFunctionReturn(0);
}
//---------------------------------------------------------------------------
#undef __FUNCT__
#define __FUNCT__ "PVOutWritePVTR"
PetscErrorCode PVOutWritePVTR(PVOut *pvout, const char *dirName)
{
	FILE        *fp;
	FDSTAG      *fs;
	char        *fname;
	OutVec      *outvecs;
	PetscInt     i, rx, ry, rz;
	PetscMPIInt  nproc, iproc;

	PetscFunctionBegin;

	// only first process generates this file (WARNING! Bottleneck!)
	if(!ISRankZero(PETSC_COMM_WORLD)) PetscFunctionReturn(0);

	// access staggered grid layout
	fs = pvout->outbuf.fs;

	// open outfile.pvtr file in the output directory (write mode)
	asprintf(&fname, "%s/%s.pvtr", dirName, pvout->outfile);
	fp = fopen(fname,"w");
	if(fp == NULL) SETERRQ1(PETSC_COMM_WORLD, 1,"cannot open file %s", fname);
	free(fname);

	// write header
	PVOutWriteXMLHeader(fp, "PRectilinearGrid");

	// open rectilinear grid data block (write total grid size)
	fprintf(fp, "\t<PRectilinearGrid GhostLevel=\"0\" WholeExtent=\"%lld %lld %lld %lld %lld %lld\">\n",
		1LL, (LLD)fs->dsx.tnods,
		1LL, (LLD)fs->dsy.tnods,
		1LL, (LLD)fs->dsz.tnods);

	// write cell data block (empty)
	fprintf(fp, "\t\t<PCellData>\n");
	fprintf(fp, "\t\t</PCellData>\n");

	// write coordinate block
	fprintf(fp, "\t\t<PCoordinates>\n");
	fprintf(fp, "\t\t\t<PDataArray type=\"Float32\" Name=\"Coordinates_X\" NumberOfComponents=\"1\" format=\"appended\"/>\n");
	fprintf(fp, "\t\t\t<PDataArray type=\"Float32\" Name=\"Coordinates_Y\" NumberOfComponents=\"1\" format=\"appended\"/>\n");
	fprintf(fp, "\t\t\t<PDataArray type=\"Float32\" Name=\"Coordinates_Z\" NumberOfComponents=\"1\" format=\"appended\"/>\n");
	fprintf(fp, "\t\t</PCoordinates>\n");

	// write description of output vectors (parameterized)
	outvecs = pvout->outvecs;
	fprintf(fp, "\t\t<PPointData>\n");
	for(i = 0; i < pvout->nvec; i++)
	{	fprintf(fp,"\t\t\t<PDataArray type=\"Float32\" Name=\"%s\" NumberOfComponents=\"%lld\" format=\"appended\"/>\n",
			outvecs[i].name, (LLD)outvecs[i].ncomp);
	}
	fprintf(fp, "\t\t</PPointData>\n");

	// get total number of sub-domains
	MPI_Comm_size(PETSC_COMM_WORLD, &nproc);

	// write local grid sizes (extents) and data file names for all sub-domains
	for(iproc = 0; iproc < nproc; iproc++)
	{
		// get sub-domain ranks in all coordinate directions
		getLocalRank(&rx, &ry, &rz, iproc, fs->dsx.nproc, fs->dsy.nproc);

		// write data
		fprintf(fp, "\t\t<Piece Extent=\"%lld %lld %lld %lld %lld %lld\" Source=\"%s_p%1.6lld.vtr\"/>\n",
			(LLD)(fs->dsx.starts[rx] + 1), (LLD)(fs->dsx.starts[rx+1] + 1),
			(LLD)(fs->dsy.starts[ry] + 1), (LLD)(fs->dsy.starts[ry+1] + 1),
			(LLD)(fs->dsz.starts[rz] + 1), (LLD)(fs->dsz.starts[rz+1] + 1), pvout->outfile, (LLD)iproc);
	}

	// close rectilinear grid data block
	fprintf(fp, "\t</PRectilinearGrid>\n");
	fprintf(fp, "</VTKFile>\n");

	// close file
	fclose(fp);
	PetscFunctionReturn(0);
}
//---------------------------------------------------------------------------
#undef __FUNCT__
#define __FUNCT__ "PVOutWriteVTR"
PetscErrorCode PVOutWriteVTR(PVOut *pvout, JacRes *jr, const char *dirName)
{
	FILE          *fp;
	FDSTAG        *fs;
	char          *fname;
	OutBuf        *outbuf;
	OutVec        *outvecs;
	PetscInt       i, rx, ry, rz, sx, sy, sz, nx, ny, nz;
	PetscMPIInt    rank;
	size_t         offset = 0;

	PetscErrorCode ierr;
	PetscFunctionBegin;

	// get global sub-domain rank
	ierr = MPI_Comm_rank(PETSC_COMM_WORLD, &rank); CHKERRQ(ierr);

	// access output buffer object & staggered grid layout
	outbuf = &pvout->outbuf;
	fs     = outbuf->fs;

	// get sizes of output grid
	GET_OUTPUT_RANGE(rx, nx, sx, fs->dsx)
	GET_OUTPUT_RANGE(ry, ny, sy, fs->dsy)
	GET_OUTPUT_RANGE(rz, nz, sz, fs->dsz)

	// open outfile_p_XXXXXX.vtr file in the output directory (write mode)
	asprintf(&fname, "%s/%s_p%1.6lld.vtr", dirName, pvout->outfile, (LLD)rank);
	fp = fopen(fname,"w");
	if(fp == NULL) SETERRQ1(PETSC_COMM_WORLD, 1,"cannot open file %s", fname);
	free(fname);

	// link output buffer to file
	OutBufConnectToFile(outbuf, fp);

	// write header
	PVOutWriteXMLHeader(fp, "RectilinearGrid");

	// open rectilinear grid data block (write total grid size)
	fprintf(fp, "\t<RectilinearGrid WholeExtent=\"%lld %lld %lld %lld %lld %lld\">\n",
		(LLD)(fs->dsx.starts[rx] + 1), (LLD)(fs->dsx.starts[rx+1] + 1),
		(LLD)(fs->dsy.starts[ry] + 1), (LLD)(fs->dsy.starts[ry+1] + 1),
		(LLD)(fs->dsz.starts[rz] + 1), (LLD)(fs->dsz.starts[rz+1] + 1));

	// open sub-domain (piece) description block
	fprintf(fp, "\t\t<Piece Extent=\"%lld %lld %lld %lld %lld %lld\">\n",
		(LLD)(fs->dsx.starts[rx] + 1), (LLD)(fs->dsx.starts[rx+1] + 1),
		(LLD)(fs->dsy.starts[ry] + 1), (LLD)(fs->dsy.starts[ry+1] + 1),
		(LLD)(fs->dsz.starts[rz] + 1), (LLD)(fs->dsz.starts[rz+1] + 1));

	// write cell data block (empty)
	fprintf(fp, "\t\t\t<CellData>\n");
	fprintf(fp, "\t\t\t</CellData>\n");

	// write coordinate block
	fprintf(fp, "\t\t\t<Coordinates>\n");

	fprintf(fp, "\t\t\t\t<DataArray type=\"Float32\" Name=\"Coordinates_X\" NumberOfComponents=\"1\" format=\"appended\" offset=\"%lld\"/>\n", (LLD)offset);
	offset += sizeof(int) + sizeof(float)*(size_t)nx;

	fprintf(fp, "\t\t\t\t<DataArray type=\"Float32\" Name=\"Coordinates_Y\" NumberOfComponents=\"1\" format=\"appended\" offset=\"%lld\"/>\n", (LLD)offset);
	offset += sizeof(int) + sizeof(float)*(size_t)ny;

	fprintf(fp, "\t\t\t\t<DataArray type=\"Float32\" Name=\"Coordinates_Z\" NumberOfComponents=\"1\" format=\"appended\" offset=\"%lld\"/>\n", (LLD)offset);
	offset += sizeof(int) + sizeof(float)*(size_t)nz;

	fprintf(fp, "\t\t\t</Coordinates>\n");

	// write description of output vectors (parameterized)
	outvecs = pvout->outvecs;
	fprintf(fp, "\t\t\t<PointData>\n");
	for(i = 0; i < pvout->nvec; i++)
	{	fprintf(fp, "\t\t\t\t<DataArray type=\"Float32\" Name=\"%s\" NumberOfComponents=\"%lld\" format=\"appended\" offset=\"%lld\"/>\n",
			outvecs[i].name, (LLD)outvecs[i].ncomp, (LLD)offset);
		// update offset
		offset += sizeof(int) + sizeof(float)*(size_t)(nx*ny*nz*outvecs[i].ncomp);
	}
	fprintf(fp, "\t\t\t</PointData>\n");

	// close sub-domain and grid blocks
	fprintf(fp, "\t\t</Piece>\n");
	fprintf(fp, "\t</RectilinearGrid>\n");

	// write appended data section
	fprintf(fp, "\t<AppendedData encoding=\"raw\">\n");
	fprintf(fp,"_");

	// coordinate vectors
	OutBufPutCoordVec(outbuf, &fs->dsx, pvout->crdScal); OutBufDump(outbuf);
	OutBufPutCoordVec(outbuf, &fs->dsy, pvout->crdScal); OutBufDump(outbuf);
	OutBufPutCoordVec(outbuf, &fs->dsz, pvout->crdScal); OutBufDump(outbuf);

	for(i = 0; i < pvout->nvec; i++)
	{
		// compute each output vector using its own setup function
		ierr = outvecs[i].OutVecFunct(jr, outbuf); CHKERRQ(ierr);
		// write vector to output file
		OutBufDump(outbuf);
	}

	// close appended data section and file
	fprintf(fp, "\n\t</AppendedData>\n");
	fprintf(fp, "</VTKFile>\n");

	// close file
	fclose(fp);

	PetscFunctionReturn(0);
}
//---------------------------------------------------------------------------
