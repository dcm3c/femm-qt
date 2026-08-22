%% Core Loss Calculation Script (Bulk Data Version)

% David Meeker
% dmeeker@ieee.org
% AGE version 18Feb2017
% Updated to use mo_getall* bulk functions for ~50x faster data retrieval

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%% Start User-Defined Parameters
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

% Model Name
MyModel = 'my_SPM_motor_AGB.fem';

% base frequency in Hz
wbase=4000/60; %(4000 rev/minute)*(minute/(60*seconds))

% range of speeds over which to evaluate losses
SpeedMin = 100; % in RPM
SpeedMax = 8000; % in RPM
SpeedStep = 100; % in RPM

% Winding properties
MyIdCurrent = 0; % direct current in phase current amplitude scaling
MyIqCurrent = 2; % quadrature current phase current amplitude scaling
MyLowestHarmonic = 2; % lowest numbered harmonic present in the stator winding
AWG=25;	% Magnet wire gauge used in winding
WindingFill=0.3882;
PhaseResistance = 0.223*2.333; % phase resistance including end turns at 20 degC
TemperatureRise = 100; % temperature increase, degrees C

% Magnet properties
RotorMagnets = 16;
omag = 0.556*10^6;					% conductivity of sintered NdFeB in S/m

% Core properties
ce = 0.530; % Eddy current coefficient in (Watt/(meter^3 * T^2 * Hz^2)
ch = 143.; % Hysteresis coefficient in (Watts/(meter^3 * T^2 * Hz)
cs = 0.95;  % Lamination stacking factor (nondimensional)

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%% End User-Defined Parameters
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

% helpful unit definitions
PI=pi; Pi=pi;
deg=Pi/180.;

% angle through which to spin the rotor in degrees
n = 360/MyLowestHarmonic;

% angle increment in degrees
dk = 1;

% A similar loss/volume expression can be derived to compute proximity
% effect losses in the windings.  Use the low frequency approximiation
% from the paper, since in this case, wire size is a lot smaller than skin
% depth at the frequencies of interest.

% Get parameters for proximity effect loss computation for phase windings
dwire=0.324861*0.0254*exp(-0.115942*AWG); % wire diameter in meters as a function of AWG
owire = (58.*10^6)/(1+TemperatureRise*0.004); % conductivity of the wire in S/m at prescribed deltaT
cePhase = (Pi^2/8)*dwire^2*WindingFill*owire;

%% Perform a series of finite element analyses

openfemm(1);
opendocument(MyModel);
mi_smartmesh(0);  % use relatively coarse mesh to save time

mi_saveas('temp.fem');

% Run an analysis through an entire spin of the rotor and record
% element centroid flux density and vector potential (using the mesh from the first iteration)
% at every step.  This information will then be used to estimate
% core losses
for kk = 1:round(n/dk)
	starttime=clock;
    k=(kk-1)*dk; % rotor angle in degrees

	% Set the rotor angle to the correct angle for this iteration
	mi_modifyboundprop('AGE',10,k);

    % make sure that the current is set to the appropriate value for this iteration.
    tta = (RotorMagnets/2)*k*deg;
	Id = [cos(tta), cos(tta-2*pi/3), cos(tta+2*pi/3)];
    Iq =-[sin(tta), sin(tta-2*pi/3), sin(tta+2*pi/3)];
	Itot =  MyIdCurrent*Id + MyIqCurrent*Iq;
    mi_setcurrent('A', Itot(1));
    mi_setcurrent('B', Itot(2));
    mi_setcurrent('C', Itot(3));

    mi_analyze(1);
    mi_loadsolution;
	mo_smooth('off');  % flux smoothing algorithm is off
    if (k == 0)
        % Record the initial mesh — bulk retrieval in one call
        elms = mo_getallelements();
        nn = size(elms, 1);
        b = zeros(floor(n/dk),nn); % matrix that will hold the flux density info
		A = zeros(floor(n/dk),nn); % matrix that will hold the vector potential info
        z = elms(:,4) + 1j*elms(:,5); % centroid locations as complex numbers
        a = elms(:,6);                 % element areas
        g = elms(:,7);                 % group numbers
        tq = zeros(floor(n/dk),1);
    end

    % Store element flux densities — bulk retrieval
    Bdata = mo_getallb();
    Adata = mo_getalla();
    for m = 1:nn
        if(g(m)>10)
			% Element is in a rotor magnet
			A(kk,m) = Adata(m);
        elseif (g(m) > 0)
			% Element is on the stator or rotor iron
            b(kk,m) = Bdata(m,1) + 1j*Bdata(m,2);
        end
    end

    % mo_getprobleminfo returns, among other things, the depth of the
    % machine in the into-the-page direction and the length units used to
    % draw the geometry. Both of these pieces of information will be needed
    % to integrate the losses over the volume of the machine.
    probinfo=mo_getprobleminfo;

	% compute torque
    tq(kk)=mo_gapintegral('AGE',0);
    mo_close;

    fprintf('% i of % i :: %f seconds ::  %f N*m \n',k,n,etime(clock,starttime),tq(kk));
end

% clean up after finite element runs are finished
closefemm;
delete('temp.fem');
delete('temp.ans');

%% Add Up Core Losses

% Compute the square of the amplitude of each harmonic at the centroid of
% each element in the mesh. Matlab's built-in FFT function makes this easy.
ns=n/dk;
bxfft=abs(fft(real(b)))*(2/ns);
byfft=abs(fft(imag(b)))*(2/ns);
bsq=(bxfft.*bxfft) + (byfft.*byfft);

% Compute the volume of each element in units of meter^3
h = probinfo(3);            % Length of the machine in the into-the-page direction
lengthunits = probinfo(4);  % Length of drawing unit in meters
v = a*h*lengthunits^2;

% compute fft of A at the center of each element
Jm=fft(A)*(2/ns);
for k=1:RotorMagnets
	g3=(g==(10+k));
	% total volume of the magnet under consideration;
	vmag=v'*g3;
	% average current in the magnet for each harmonic
	Jo=(Jm*(v.*g3))/vmag;
	% subtract averages off of each each element in the magnet
	Jm = Jm - Jo*g3';
end

Iphase=sqrt(MyIdCurrent^2+MyIqCurrent^2)/sqrt(2);
PhaseOhmic = 3*(PhaseResistance*(1+TemperatureRise*0.004))*Iphase^2;

results=[];

for thisSpeed=SpeedMin:SpeedStep:SpeedMax

	thisFrequency = thisSpeed/60; % mechanical speed in Hz

	% Make a vector representing the frequency associated with each harmonic
	% The last half of the entries are zeroed out so that we don't count each
	% harmonic twice--the upper half of the FFT a mirror of the lower half
	w=0:(ns-1);
	w=MyLowestHarmonic*thisFrequency*w.*(w<(ns/2));

	% Now, total core loss can be computed in one fell swoop...
	% Dividing the result by cs corrects for the lamination stacking factor
	g1=(g==10);
	rotor_loss = ((ch*w+ce*w.*w)*bsq*(v.*g1))/cs;

	g2=(g==1);
	stator_loss = ((ch*w+ce*w.*w)*bsq*(v.*g2))/cs;

	% and prox losses can be totalled up in a similar way
	g4=(g==2);
	prox_loss = ((cePhase*w.*w)*bsq*(v.*g4));

	% Add up eddy current losses in the magnets
	magnet_loss = (1/2)*((omag*(2*pi*w).^2)*(abs(Jm).^2)*v);

	total_loss = rotor_loss + stator_loss + prox_loss + PhaseOhmic + magnet_loss;

	results = [results; thisSpeed, rotor_loss, stator_loss, magnet_loss, PhaseOhmic, prox_loss, total_loss];
end

% save('c:\\temp\\myLossData.m','results','-ASCII');

%% Loss plot
plot(results(:,1),results(:,2:7));
xlabel('Speed, RPM');
ylabel('Total Losses, Watts');
title('Loss versus Speed');
legend('Rotor Core','Stator Core','Magnets','Coil Ohmic','Coil Proximity','Total Loss','Location','northwest');
