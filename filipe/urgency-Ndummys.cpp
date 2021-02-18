/**
 * @file
 *
 * \brief A hospital pediatric urgency with a Manchester triage system.
 */

#include  <stdio.h>
#include  <stdlib.h>
#include  <string.h>
#include  <libgen.h>
#include  <unistd.h>
#include  <sys/wait.h>
#include  <sys/types.h>
#include  <thread.h>
#include  <math.h>
#include  <stdint.h>
#include  <signal.h>
#include  <utils.h>
#include  "settings.h"
#include  "pfifo.h"

//#include "thread.h"
//#include "process.h"

#include <iostream>

#define USAGE "Synopsis: %s [options]\n" \
   "\t----------+-------------------------------------------\n" \
   "\t  Option  |          Description                      \n" \
   "\t----------+-------------------------------------------\n" \
   "\t -p num   | number of patients (dfl: 4)               \n" \
   "\t -n num   | number of nurses (dfl: 1)                 \n" \
   "\t -d num   | number of doctors (dfl: 1)                \n" \
   "\t -h       | this help                                 \n" \
   "\t----------+-------------------------------------------\n"

/**
 * \brief Patient data structure
 */
 
pthread_mutex_t accessCR = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t atendimento[MAX_PATIENTS];

typedef struct
{
   char name[MAX_NAME+1];
   int done; // 0: waiting for consultation; 1: consultation finished
} Patient;

typedef struct
{
    unsigned int id;      ///< thread id
    uint32_t dummy;	//nº de dummys
} ARGV;		//para a main gerir os varios pacientes, medicos e enfermeiros. NOTA: enfiar o crl do int no crl do void não dá!


typedef struct
{
    int num_patients;
    Patient all_patients[MAX_PATIENTS];
    PriorityFIFO triage_queue;
    PriorityFIFO doctor_queue;
} HospitalData;

HospitalData * hd = NULL;

/**
 *  \brief patient verification test
 */
#define check_valid_patient(id) do { check_valid_id(id); check_valid_name(hd->all_patients[id].name); } while(0)

int random_manchester_triage_priority();
void new_patient(Patient* patient); // initializes a new patient
void random_wait();

/* ************************************************* */

/* changes may be required to this function */
void init_simulation(uint32_t np)
{
   mutex_lock(&accessCR);	
	
   printf("Initializing simulation\n");
   hd = (HospitalData*)mem_alloc(sizeof(HospitalData)); // mem_alloc is a malloc with NULL pointer verification
   memset(hd, 0, sizeof(HospitalData));
   hd->num_patients = np;
   init_pfifo(&hd->triage_queue);
   init_pfifo(&hd->doctor_queue);
   
   mutex_unlock(&accessCR);
}

/* ************************************************* */

void nurse_iteration(int id, uint32_t ndummy)
{
   while(true){
    
   printf("\e[34;01mNurse %u: get next patient\e[0m\n", id);
   uint32_t patient = retrieve_pfifo(&hd->triage_queue);
   check_valid_patient(patient);
   printf("\e[34;01mNurse %u: evaluate patient %u priority\e[0m\n", id, patient);
   uint32_t priority;
   
   if(patient > MAX_ID - ndummy){	//Os indices dos dummys sao atribuidos apartir de MAX_ID-ndummy.
   	priority = 16; //prioridade minima atribuida ao dummy
   }else{
   	priority = random_manchester_triage_priority(); 
   }
   
   printf("\e[34;01mNurse %u: add patient %u with priority %u to doctor queue\e[0m\n", id, patient, priority);
   insert_pfifo(&hd->doctor_queue, patient, priority);
   
   if(patient > MAX_ID - ndummy){	//Independentemente do numero de pacientes o ultimo a ser observado pela nurse é sempre o paciente dummy, 
   					//porque eu lanço a thread do dummy antes da dos pacientes, logo o dummy vai ser o ultimo paciente atendido pela nurse.
   	break;		
   }
   
   }
}

void *nurse(void *argn){
	
	ARGV* n = (ARGV*)argn;
	printf("\e[34;01mnurse %d arrived to work\e[0m\n",n->id);
	nurse_iteration(n->id,n->dummy);
	printf("\e[35;01mNurse %u finish\e[0m\n",n->id);
	
	return NULL;
}


/* ************************************************* */

void doctor_iteration()
{
   while(true){
   
   printf("\e[32;01mDoctor: get next patient\e[0m\n");
   uint32_t patient = retrieve_pfifo(&hd->doctor_queue);
   
   mutex_lock(&accessCR);
   
   check_valid_patient(patient);
   printf("\e[32;01mDoctor: treat patient %u\e[0m\n", patient);
   random_wait();
   printf("\e[32;01mDoctor: patient %u treated\e[0m\n", patient);
   hd->all_patients[patient].done = 1;
   cond_broadcast(&atendimento[patient]);
   printf("\e[35;01mcond_broadcast do paciente  %s, %u\e[0m\n",hd->all_patients[patient].name,patient);
   
   mutex_unlock(&accessCR);
   
   //1ª forma para finalizar o doctor		// !!!!!!!!!!!!!!!!!!para valores de npatients muito grandes este for nao é finalizado!!!!!!!!!!!!!!!!!!!!!!!!!
   
   /*int patients_treated = 0;
   for(int i = 0; i < MAX_PATIENTS; i++){		
   	if(hd->all_patients[i].done == 1){		// verificação do numero de pacientes que ja foram tratados
   		patients_treated++;
   	}
   }
 
   printf("%u, %u\n",patients_treated,hd->num_patients);
   if(patients_treated == hd->num_patients){		//o medico acaba quando os pacientes ja foram todos tratados
   
   	break;
   }*/
   
   //2ª forma de finalizar o doctor independentemente do numero de pacientes
   
   if(patient == MAX_ID){		//O medico acaba quando os pacientes ja foram todos tratados. O dummy é o ultimo a entrar para a sala 
   	break;				//de espera do doctor e tem a prioridade minima, logo é o ultimo a ser atendido pelo medico.
   }
   
   }

}

void *doctor(void *argd){

	ARGV* d = (ARGV*)argd;
	printf("\e[32;01m doctor %d arrived to work\e[0m\n",d->id);
	doctor_iteration();
	printf("\e[35;01mDoctor finish\e[0m\n");
	
	return NULL;

}

/* ************************************************* */

void patient_goto_urgency(int id)
{
   new_patient(&hd->all_patients[id]);
   check_valid_name(hd->all_patients[id].name);
   printf("\e[30;01mPatient %s (number %u): get to hospital\e[0m\n", hd->all_patients[id].name, id);
   insert_pfifo(&hd->triage_queue, id, 1); // all elements in triage queue with the same priority!
}

/* changes may be required to this function */
void patient_wait_end_of_consultation(int id)
{
   mutex_lock(&accessCR);
   
   check_valid_name(hd->all_patients[id].name);
   while(!hd->all_patients[id].done){		//condição associada à finalização da consulta
	cond_wait(&atendimento[id],&accessCR); //fazer o brodcast disto no doctor
   }
   printf("\e[30;01mPatient %s (number %u): health problems treated\e[0m\n", hd->all_patients[id].name, id);
   
   mutex_unlock(&accessCR);
}

/* changes are required to this function */
void patient_life(int id)
{
   patient_goto_urgency(id);
   patient_wait_end_of_consultation(id);
   memset(&(hd->all_patients[id]), 0, sizeof(Patient)); // patient finished
}

void *patient(void *argp){

	ARGV* p = (ARGV*)argp;
	printf("\e[30;01mPatient %s (number %u): runing threat\e[0m\n", hd->all_patients[p->id].name, p->id);
	patient_life(p->id);
	
	return NULL;
}

/* ************************************************* */

int main(int argc, char *argv[])
{
   uint32_t npatients = 20;  ///< number of patients
   uint32_t nnurses = 3;    ///< number of triage nurses
   uint32_t ndoctors = 1;   ///< number of doctors
   uint32_t ndummys = nnurses;   ///< number of dummys

   /* command line processing */
   int option;
   while ((option = getopt(argc, argv, "p:n:d:h")) != -1)
   {
      switch (option)
      {
         case 'p':
            npatients = atoi(optarg);
            if (npatients < 1 || npatients > MAX_PATIENTS)
            {
               fprintf(stderr, "Invalid number of patients!\n");
               return EXIT_FAILURE;
            }
            break;
         case 'n':
            nnurses = atoi(optarg);
            if (nnurses < 1)
            {
               fprintf(stderr, "Invalid number of nurses!\n");
               return EXIT_FAILURE;
            }
            break;
         case 'd':
            ndoctors = atoi(optarg);
            if (ndoctors < 1)
            {
               fprintf(stderr, "Invalid number of doctors!\n");
               return EXIT_FAILURE;
            }
            break;
         case 'h':
            printf(USAGE, basename(argv[0]));
            return EXIT_SUCCESS;
         default:
            fprintf(stderr, "Non valid option!\n");
            fprintf(stderr, USAGE, basename(argv[0]));
            return EXIT_FAILURE;
      }
   }

   /* start random generator */
   srand(getpid());

   /* init simulation */
   init_simulation(npatients + ndummys); //npatients + dummy
   
   	//iniciar as variaveis de condição
	for (unsigned int i = 0; i < MAX_PATIENTS; i++) {
		atendimento[i] = PTHREAD_COND_INITIALIZER;
	}
	
	/* launching the patient dummy */       // crio este primeiro para ser o ultimo a ser atendido pela nurse
	pthread_t pthr[npatients+ndummys]; 	// dummy and patients' ids
	ARGV p_ARGV[npatients+ndummys];
	
	for (unsigned int i = npatients; i < npatients + ndummys; i++){
	 	p_ARGV[i].id = MAX_ID - (i - npatients);
	 	
		thread_create(&pthr[i],NULL,patient,&p_ARGV[i]);
		printf(">> Patient thread %d has started <<\n", p_ARGV[i].id);
  	}
  	/* launching the patients */
	
	for (unsigned int i = 0; i < npatients; i++){
		p_ARGV[i].id = i;
		thread_create(&pthr[i],NULL,patient,&p_ARGV[i]);
		printf(">> Patient thread %d has started <<\n", p_ARGV[i].id);
	}
   
   	/* launching the nurse */
	pthread_t nthr[nnurses]; 	// nurses' ids
	ARGV n_ARGV[nnurses];
	
	for (unsigned int i = 0; i < nnurses; i++){
		n_ARGV[i].id = i;
		n_ARGV[i].dummy = ndummys;
		thread_create(&nthr[i],NULL,nurse,&n_ARGV[i]);		//hd->num_patients
		printf("Nurse thread %d has started\n", n_ARGV[i].id);
	}
		
	/* launching the doctor */
	pthread_t dthr[ndoctors]; 	// doctors' ids
	ARGV d_ARGV[ndoctors];
	
	for (unsigned int i = 0; i < ndoctors; i++){
		d_ARGV[i].id = i;
		thread_create(&dthr[i],NULL,doctor,&d_ARGV[i]);
		printf("Doctor thread %d has started\n", d_ARGV[i].id);
	}

   	/* wait for threads to conclude */
   	for (unsigned int i = npatients; i < npatients + ndummys; i++){
		thread_join(pthr[i], NULL);
		printf(">> Dummy %u thread has terminated <<\n", i-npatients);
   	}
   	
	for (unsigned int i = 0; i < npatients; i++) {
		thread_join(pthr[i], NULL);
		printf(">> Patient thread %d has terminated <<\n", i);
	}

	for (unsigned int i = 0; i < nnurses; i++){
		thread_join(nthr[i],NULL);
		printf("Nurse thread %d has terminated\n", i);
	}
	
	for (unsigned int i = 0; i < ndoctors; i++){
		thread_join(dthr[i],NULL);
		printf("Doctor thread %d has terminated\n", i);
	}

   return EXIT_SUCCESS;
}


/* YOU MAY IGNORE THE FOLLOWING CODE */

int random_manchester_triage_priority()
{
   int result;
   int perc = (int)(100*(double)rand()/((double)RAND_MAX)); // in [0;100]
   if (perc < 10)
      result = RED;
   else if (perc < 30)
      result = ORANGE;
   else if (perc < 50)
      result = YELLOW;
   else if (perc < 75)
      result = GREEN;
   else
      result = BLUE;
   return result;
}

static char **names = (char *[]) {"Ana", "Miguel", "Luis", "Joao", "Artur", "Maria", "Luisa", "Mario", "Augusto", "Antonio", "Jose", "Alice", "Almerindo", "Gustavo", "Paulo", "Paula", NULL};

char* random_name()
{
   static int names_len = 0;
   if (names_len == 0)
   {
      for(names_len = 0; names[names_len] != NULL; names_len++)
         ;
   }

   return names[(int)(names_len*(double)rand()/((double)RAND_MAX+1))];
}

void new_patient(Patient* patient)
{
   strcpy(patient->name, random_name());
   patient->done = 0;
}

void random_wait()
{
   usleep((useconds_t)(MAX_WAIT*(double)rand()/(double)RAND_MAX));
}

